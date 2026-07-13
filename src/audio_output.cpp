#include "audio_output.h"

#include <Arduino.h>
#include <math.h>

#include "driver/sigmadelta.h"

namespace
{

constexpr uint32_t SAMPLE_RATE_HZ = 38400;
constexpr uint32_t TIMER_CLOCK_HZ = 20000000;

// Sigma-delta duty is signed 8-bit; the AT1846S mic input distorts above
// roughly +/-85 (value taken from the working ESP32APRS AFSK code).
constexpr int32_t PEAK_DUTY = 85;

// Amplitude envelope: full rise/fall in ~5 ms to avoid key clicks.
constexpr int32_t RAMP_SAMPLES = 192;
constexpr int32_t AMPLITUDE_STEP_Q8 = (PEAK_DUTY << 8) / RAMP_SAMPLES;

// Voice ring buffer: at a 16 kHz file rate this holds ~250 ms, plenty of
// cushion over SD card read latency. Must be a power of two.
constexpr size_t RING_SIZE = 4096;
constexpr uint32_t RING_MASK = RING_SIZE - 1;

enum SourceMode : uint8_t
{
    MODE_TONE,
    MODE_VOICE,
};

int8_t sine_table[256];
int16_t voice_ring[RING_SIZE];

hw_timer_t *sample_timer = nullptr;
portMUX_TYPE audio_mux = portMUX_INITIALIZER_UNLOCKED;

volatile SourceMode source_mode = MODE_TONE;

// Envelope, shared by both sources.
volatile int32_t amplitude_q8 = 0;
volatile int32_t target_amplitude_q8 = 0;

// Tone state. phase_increment is only written while the timer is stopped.
volatile uint32_t phase_accumulator = 0;
uint32_t phase_increment = 0;

// Voice state. The step, gain, current/next pair and position are only
// written inside a critical section or while the ISR is in tone mode.
volatile uint32_t ring_head = 0; // written by the main task
volatile uint32_t ring_tail = 0; // consumed by the ISR
volatile uint32_t voice_pos_q16 = 0;
uint32_t voice_step_q16 = 0;
volatile int16_t voice_current = 0;
volatile int16_t voice_next = 0;
int32_t voice_gain = 0;

// Sub-audible pilot mixed into voice mode (keeps the mic AGC loaded).
volatile uint32_t pilot_phase = 0;
uint32_t pilot_increment = 0;
int32_t pilot_level_q8 = 0;

// Sub-LSB residue carried between samples for noise-shaped quantization
// (only the ISR touches it).
int32_t quantization_error_q8 = 0;

void IRAM_ATTR sample_isr()
{
    int32_t amplitude = amplitude_q8;
    const int32_t target = target_amplitude_q8;
    if (amplitude < target)
    {
        amplitude += AMPLITUDE_STEP_Q8;
        if (amplitude > target)
            amplitude = target;
    }
    else if (amplitude > target)
    {
        amplitude -= AMPLITUDE_STEP_Q8;
        if (amplitude < target)
            amplitude = target;
    }
    amplitude_q8 = amplitude;

    // Compute the desired output in Q8 (duty x 256) so sub-LSB precision
    // survives until the final quantization step.
    int32_t desired_q8;
    if (source_mode == MODE_VOICE)
    {
        uint32_t position = voice_pos_q16 + voice_step_q16;
        while (position >= 0x10000)
        {
            position -= 0x10000;
            voice_current = voice_next;
            if (ring_tail != ring_head)
            {
                voice_next = voice_ring[ring_tail & RING_MASK];
                ring_tail = ring_tail + 1;
            }
            else
            {
                voice_next = 0; // underrun or end of data: fade to silence
            }
        }
        voice_pos_q16 = position;
        const int32_t interpolated =
            voice_current + (((voice_next - voice_current) * static_cast<int32_t>(position)) >> 16);
        const int32_t scaled_q8 = (interpolated * voice_gain) >> 8;

        pilot_phase += pilot_increment;
        const int32_t pilot_q8 =
            (sine_table[pilot_phase >> 24] * pilot_level_q8) >> 7;

        desired_q8 = ((scaled_q8 + pilot_q8) * (amplitude >> 8)) / PEAK_DUTY;
    }
    else
    {
        phase_accumulator += phase_increment;
        const int32_t sine_sample = sine_table[phase_accumulator >> 24];
        desired_q8 = (sine_sample * amplitude) >> 7;
    }

    if (desired_q8 == 0 && amplitude == 0)
        quantization_error_q8 = 0; // true silence, no residue dithering

    // First-order noise shaping: feed the sub-LSB quantization error into
    // the next sample, pushing the 8-bit duty's error spectrum above the
    // audio band where the SA868's TX low-pass filter removes it.
    const int32_t shaped_q8 = desired_q8 + quantization_error_q8;
    int32_t duty = shaped_q8 >> 8;
    if (duty > 127)
        duty = 127;
    else if (duty < -127)
        duty = -127;
    quantization_error_q8 = shaped_q8 - (duty << 8);
    if (quantization_error_q8 > 256) // stop windup while clipped
        quantization_error_q8 = 256;
    else if (quantization_error_q8 < -256)
        quantization_error_q8 = -256;

    sigmadelta_set_duty(SIGMADELTA_CHANNEL_0, static_cast<int8_t>(duty));
}

} // namespace

void AudioOutput::begin(int8_t outputPin, uint32_t toneHz)
{
    for (int index = 0; index < 256; index++)
    {
        const double angle = 2.0 * M_PI * index / 256.0;
        sine_table[index] = static_cast<int8_t>(lround(sin(angle) * 127.0));
    }

    phase_increment =
        static_cast<uint32_t>((static_cast<uint64_t>(toneHz) << 32) / SAMPLE_RATE_HZ);

    // Prescale 3 clocks the modulator at 20 MHz (80 MHz / (3+1)), putting
    // the idle pulse energy near 10 MHz where the board's RC filter kills
    // it. The ESP32APRS value of 96 (~825 kHz) leaves the idle pattern at
    // ~412 kHz, which reaches the SA868 mic input and aliases into the
    // audio band as hiss - amplified further by the mic AGC.
    sigmadelta_config_t sigmadelta_cfg = {
        .channel = SIGMADELTA_CHANNEL_0,
        .sigmadelta_duty = 0,
        .sigmadelta_prescale = 3,
        .sigmadelta_gpio = static_cast<gpio_num_t>(outputPin),
    };
    sigmadelta_config(&sigmadelta_cfg);

    sample_timer = timerBegin(TIMER_CLOCK_HZ);
    timerAttachInterrupt(sample_timer, &sample_isr);
    timerAlarm(sample_timer, TIMER_CLOCK_HZ / SAMPLE_RATE_HZ, true, 0);
    timerStop(sample_timer);
}

void AudioOutput::enable()
{
    phase_accumulator = 0;
    amplitude_q8 = 0;
    target_amplitude_q8 = 0;
    source_mode = MODE_TONE;
    timerStart(sample_timer);
}

void AudioOutput::disable()
{
    timerStop(sample_timer);
    target_amplitude_q8 = 0;
    amplitude_q8 = 0;
    source_mode = MODE_TONE;
    sigmadelta_set_duty(SIGMADELTA_CHANNEL_0, 0);
}

void AudioOutput::keyDown()
{
    target_amplitude_q8 = PEAK_DUTY << 8;
}

void AudioOutput::keyUp()
{
    target_amplitude_q8 = 0;
}

void AudioOutput::startVoice(uint32_t sampleRateHz, int32_t gain,
                             uint32_t pilotHz, int32_t pilotLevel)
{
    portENTER_CRITICAL(&audio_mux);
    ring_head = 0;
    ring_tail = 0;
    voice_pos_q16 = 0;
    voice_current = 0;
    voice_next = 0;
    voice_step_q16 = static_cast<uint32_t>(
        (static_cast<uint64_t>(sampleRateHz) << 16) / SAMPLE_RATE_HZ);
    voice_gain = gain;
    pilot_phase = 0;
    pilot_increment = static_cast<uint32_t>(
        (static_cast<uint64_t>(pilotHz) << 32) / SAMPLE_RATE_HZ);
    pilot_level_q8 = pilotLevel << 8;
    source_mode = MODE_VOICE;
    portEXIT_CRITICAL(&audio_mux);
    keyDown(); // ramp the envelope up
}

size_t AudioOutput::queueVoice(const int16_t *samples, size_t count)
{
    const uint32_t used = ring_head - ring_tail;
    const size_t space = RING_SIZE - used;
    if (count > space)
        count = space;
    const uint32_t head = ring_head;
    for (size_t index = 0; index < count; index++)
        voice_ring[(head + index) & RING_MASK] = samples[index];
    ring_head = head + count; // publish only after the samples are written
    return count;
}

bool AudioOutput::voiceDrained() const
{
    return ring_head == ring_tail;
}

void AudioOutput::endVoice()
{
    keyUp();
    delay(10); // let the envelope ramp finish
    portENTER_CRITICAL(&audio_mux);
    source_mode = MODE_TONE;
    portEXIT_CRITICAL(&audio_mux);
}

void AudioOutput::abortVoice()
{
    portENTER_CRITICAL(&audio_mux);
    ring_head = 0;
    ring_tail = 0;
    portEXIT_CRITICAL(&audio_mux);
    endVoice();
}
