// Transmit audio generation: the ESP32-S3 I2S peripheral in PDM TX
// mode, driving the SA868 mic input on GPIO 18.
//
// The hardware PDM modulator takes full 16-bit samples from DMA, so
// this backend has no per-sample interrupt, no software noise shaping,
// and no fractional resampler - the I2S clock is set to each WAV's
// native rate. The morse tone is rendered as sample blocks by a small
// task; voice playback writes blocks directly into the DMA queue from
// the caller's context (queueVoice blocks until accepted).
//
#include "audio_output.h"

#include <Arduino.h>
#include <math.h>

#include <driver/i2s_pdm.h>

namespace
{

constexpr uint32_t TONE_SAMPLE_RATE_HZ = 16000;
constexpr size_t TONE_BLOCK_FRAMES = 256;  // 16 ms per block at 16 kHz
constexpr size_t VOICE_BLOCK_FRAMES = 512;
// Cap per queueVoice() call so the caller's abort check runs regularly.
// Kept small: with a 2048-frame cap, a regular ~8/s tick was audible in
// the transmitted audio, matching the call cadence (2048 frames at
// 16 kHz = 128 ms) - some boundary artifact of the burstier feed
// pattern. One DMA-write block per call spreads the work evenly.
constexpr size_t VOICE_MAX_ACCEPT = 512;

// Full-scale mapping: the sigma-delta backend drives +/-85 of its
// +/-127 duty range at VOICE_GAIN 170. sample*gain/254 reproduces the
// same 0.67 of full scale here (32767*170/254 ~= 21930).
constexpr int32_t GAIN_DIVISOR = 254;

// Envelope in Q8, same scale as the sigma-delta backend (peak 85<<8),
// ramped over ~5 ms to keep morse keying click-free.
constexpr int32_t PEAK_AMPLITUDE = 85;

enum SourceMode : uint8_t
{
    MODE_TONE,
    MODE_VOICE,
};

int8_t sine_table[256];
int8_t data_pin = -1;
uint32_t tone_hz = 800;

i2s_chan_handle_t tx_channel = nullptr;
TaskHandle_t render_task = nullptr;

volatile SourceMode source_mode = MODE_TONE;
volatile bool channel_running = false;
volatile bool park_request = false;
volatile bool task_parked = true;

uint32_t current_rate = TONE_SAMPLE_RATE_HZ;
uint32_t phase_increment = 0;
int32_t ramp_step_q8 = 1;
volatile uint32_t phase_accumulator = 0;
volatile int32_t amplitude_q8 = 0;
volatile int32_t target_amplitude_q8 = 0;

int32_t voice_gain = 0;
volatile uint64_t voice_bytes_queued = 0;
volatile uint64_t voice_bytes_sent = 0;

// DMA completion callback: tracks how much queued voice audio has
// actually left for the radio, so voiceDrained() is exact.
bool IRAM_ATTR on_sent(i2s_chan_handle_t, i2s_event_data_t *event, void *)
{
    if (source_mode == MODE_VOICE)
        voice_bytes_sent += event->size;
    return false;
}

// Advances the shared envelope by one sample; returns amplitude 0..85.
inline int32_t envelope_step()
{
    int32_t amplitude = amplitude_q8;
    const int32_t target = target_amplitude_q8;
    if (amplitude < target)
    {
        amplitude += ramp_step_q8;
        if (amplitude > target)
            amplitude = target;
    }
    else if (amplitude > target)
    {
        amplitude -= ramp_step_q8;
        if (amplitude < target)
            amplitude = target;
    }
    amplitude_q8 = amplitude;
    return amplitude >> 8;
}

void set_clock(uint32_t sample_rate)
{
    // The IDF driver overwrites the fractional MCLK divider with only
    // its integer part (a PDM noise workaround in i2s_pdm.c), so the
    // realized frame rate is sclk / (floor(mclk_div) * bclk_div * 64 *
    // osr) - up to several percent of pitch error if taken as-is (the
    // 16 kHz default lands 8.5% sharp). Both osr (= fp/fs) and bclk_div
    // are free choices, so search them for the combination whose
    // integer-divided rate lands closest to the requested one, breaking
    // ties toward the highest PDM carrier (better RC suppression).
    constexpr uint64_t SCLK_HZ = 160000000ULL; // I2S_CLK_SRC_DEFAULT PLL
    const int osr_options[] = {2, 3, 4, 5, 6, 8};
    uint32_t best_fs = 480;
    uint32_t best_bclk_div = 8;
    uint64_t best_carrier = 0;
    double best_error_hz = 1e12;
    double realized_hz = sample_rate;
    for (const int osr : osr_options)
    {
        const uint64_t bclk = static_cast<uint64_t>(sample_rate) * 64 * osr;
        for (uint32_t bclk_div = 8; bclk_div < 256; bclk_div++)
        {
            const uint64_t mclk = bclk * bclk_div;
            if (static_cast<double>(SCLK_HZ) <= static_cast<double>(mclk) * 1.99)
                break; // driver would reject: sample rate too large
            const uint32_t mclk_div = static_cast<uint32_t>(SCLK_HZ / mclk);
            if (mclk_div < 2 || mclk_div >= 256)
                continue;
            const double realized = static_cast<double>(SCLK_HZ) /
                                    (static_cast<double>(mclk_div) * bclk_div * 64 * osr);
            const double error_hz = fabs(realized - sample_rate);
            const uint64_t carrier = SCLK_HZ / (static_cast<uint64_t>(mclk_div) * bclk_div);
            const bool better = error_hz < best_error_hz - 0.5 ||
                                (error_hz < best_error_hz + 0.5 && carrier > best_carrier);
            if (better)
            {
                best_error_hz = error_hz;
                best_fs = 960 / osr;
                best_bclk_div = bclk_div;
                best_carrier = carrier;
                realized_hz = realized;
            }
        }
    }

    i2s_pdm_tx_clk_config_t clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(sample_rate);
    clk_cfg.up_sample_fp = 960;
    clk_cfg.up_sample_fs = best_fs;
    clk_cfg.bclk_div = best_bclk_div;
    Serial.printf("I2S PDM clock: %lu Hz -> %.1f Hz realized (%.2f%% error), "
                  "carrier %.2f MHz (osr %lu, bclk_div %lu)\n",
                  static_cast<unsigned long>(sample_rate), realized_hz,
                  100.0 * (realized_hz - sample_rate) / sample_rate,
                  best_carrier / 1e6, static_cast<unsigned long>(960 / best_fs),
                  static_cast<unsigned long>(best_bclk_div));
    const esp_err_t err = i2s_channel_reconfig_pdm_tx_clock(tx_channel, &clk_cfg);
    if (err != ESP_OK)
        Serial.printf("I2S PDM: clock reconfig to %lu Hz failed (%d)\n",
                      static_cast<unsigned long>(sample_rate), err);
    current_rate = sample_rate;
    phase_increment = static_cast<uint32_t>(
        (static_cast<uint64_t>(tone_hz) << 32) / sample_rate);
    ramp_step_q8 = (PEAK_AMPLITUDE << 8) / (static_cast<int32_t>(sample_rate) / 200);
    if (ramp_step_q8 < 1)
        ramp_step_q8 = 1;
}

// Renders the keyed morse sine (or silence) whenever voice is not
// active. i2s_channel_write blocks on DMA space, which paces the task.
void render_tone_task(void *)
{
    static int16_t block[TONE_BLOCK_FRAMES * 2];
    for (;;)
    {
        if (park_request || !channel_running || source_mode != MODE_TONE)
        {
            task_parked = true;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        task_parked = false;

        for (size_t frame = 0; frame < TONE_BLOCK_FRAMES; frame++)
        {
            const int32_t amplitude = envelope_step();
            phase_accumulator += phase_increment;
            const int32_t sine_sample = sine_table[phase_accumulator >> 24];
            // sine(+/-127) * amplitude(0..85) * 2 peaks at ~21590,
            // matching the voice full-scale mapping above.
            const int16_t value = static_cast<int16_t>(sine_sample * amplitude * 2);
            block[frame * 2] = value;
            block[frame * 2 + 1] = value;
        }
        size_t written = 0;
        i2s_channel_write(tx_channel, block, sizeof(block), &written, portMAX_DELAY);
    }
}

void park_render_task()
{
    park_request = true;
    while (!task_parked)
        delay(1);
}

} // namespace

void AudioOutput::begin(int8_t outputPin, uint32_t toneHz)
{
    data_pin = outputPin;
    tone_hz = toneHz;
    for (int index = 0; index < 256; index++)
    {
        const double angle = 2.0 * M_PI * index / 256.0;
        sine_table[index] = static_cast<int8_t>(lround(sin(angle) * 127.0));
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_channel, nullptr));

    // STEREO with every sample duplicated: the S3's PDM TX hardware
    // always emits two slots per frame (total_slot = 2 in the driver),
    // so mono DMA data drains at twice the sample rate - one octave
    // high. Feeding identical L/R samples restores correct pitch.
    i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(TONE_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .clk = I2S_GPIO_UNUSED,
            .dout = static_cast<gpio_num_t>(data_pin),
            .invert_flags = { .clk_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_tx_mode(tx_channel, &pdm_cfg));

    i2s_event_callbacks_t callbacks = {
        .on_recv = nullptr,
        .on_recv_q_ovf = nullptr,
        .on_sent = on_sent,
        .on_send_q_ovf = nullptr,
    };
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx_channel, &callbacks, nullptr));

    set_clock(TONE_SAMPLE_RATE_HZ);

    xTaskCreatePinnedToCore(render_tone_task, "fox_audio", 4096, nullptr, 3,
                            &render_task, 1);
    Serial.println("Audio backend: I2S PDM (DMA)");
}

void AudioOutput::enable()
{
    phase_accumulator = 0;
    amplitude_q8 = 0;
    target_amplitude_q8 = 0;
    source_mode = MODE_TONE;
    ESP_ERROR_CHECK(i2s_channel_enable(tx_channel));
    channel_running = true;
    park_request = false;
}

void AudioOutput::disable()
{
    park_render_task();
    channel_running = false;
    i2s_channel_disable(tx_channel);
    amplitude_q8 = 0;
    target_amplitude_q8 = 0;
    source_mode = MODE_TONE;
}

void AudioOutput::keyDown()
{
    target_amplitude_q8 = PEAK_AMPLITUDE << 8;
}

void AudioOutput::keyUp()
{
    target_amplitude_q8 = 0;
}

void AudioOutput::startVoice(uint32_t sampleRateHz, int32_t gain)
{
    park_render_task();
    i2s_channel_disable(tx_channel);
    set_clock(sampleRateHz);
    voice_gain = gain;
    voice_bytes_queued = 0;
    voice_bytes_sent = 0;
    amplitude_q8 = 0;
    target_amplitude_q8 = PEAK_AMPLITUDE << 8; // ramp in over ~5 ms
    source_mode = MODE_VOICE;
    ESP_ERROR_CHECK(i2s_channel_enable(tx_channel));
}

size_t AudioOutput::queueVoice(const int16_t *samples, size_t count)
{
    static int16_t block[VOICE_BLOCK_FRAMES * 2];

    if (count > VOICE_MAX_ACCEPT)
        count = VOICE_MAX_ACCEPT; // let the caller's abort check run

    size_t done = 0;
    while (done < count)
    {
        size_t frame_count = count - done;
        if (frame_count > VOICE_BLOCK_FRAMES)
            frame_count = VOICE_BLOCK_FRAMES;
        for (size_t frame = 0; frame < frame_count; frame++)
        {
            const int32_t amplitude = envelope_step();
            int32_t value = (samples[done + frame] * voice_gain) / GAIN_DIVISOR;
            value = value * amplitude / PEAK_AMPLITUDE;
            block[frame * 2] = static_cast<int16_t>(value);
            block[frame * 2 + 1] = static_cast<int16_t>(value);
        }
        size_t written = 0;
        i2s_channel_write(tx_channel, block, frame_count * 2 * sizeof(int16_t),
                          &written, portMAX_DELAY);
        voice_bytes_queued += written;
        done += frame_count;
    }
    return count;
}

bool AudioOutput::voiceDrained() const
{
    return voice_bytes_sent >= voice_bytes_queued;
}

void AudioOutput::endVoice()
{
    // Recordings end in trimmed near-silence, so no fade-out is queued.
    i2s_channel_disable(tx_channel);
    set_clock(TONE_SAMPLE_RATE_HZ);
    amplitude_q8 = 0;
    target_amplitude_q8 = 0;
    source_mode = MODE_TONE;
    ESP_ERROR_CHECK(i2s_channel_enable(tx_channel));
    park_request = false; // tone task resumes rendering silence
}

void AudioOutput::abortVoice()
{
    // Disabling the channel drops all queued DMA audio immediately.
    endVoice();
}
