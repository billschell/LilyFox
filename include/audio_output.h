#pragma once

#include <stddef.h>
#include <stdint.h>

// Generates transmit audio on the SA868 mic input using the ESP32-S3
// sigma-delta modulator, driven by a 38.4 kHz hardware timer ISR (the
// same technique the ESP32APRS firmware uses for AFSK).
//
// Two sample sources share the ISR:
//  - Tone: a keyed sine for morse (keyDown / keyUp).
//  - Voice: 16-bit PCM streamed through a ring buffer, fractionally
//    resampled from the file's rate to the ISR rate with linear
//    interpolation, so any source sample rate plays correctly.
//
// Both sources pass through a ~5 ms amplitude envelope so keying and
// playback start/stop are click-free.
//
// Only one instance may exist: the sample ISR uses file-scope state.
class AudioOutput
{
public:
    // Configures the sigma-delta channel and sample timer. The timer is
    // left stopped; call enable() before keying or playing.
    void begin(int8_t outputPin, uint32_t toneHz);

    // Start/stop the 38.4 kHz sample ISR. Enable only while transmitting
    // so the CPU is idle between cycles.
    void enable();
    void disable();

    // Tone source (morse keying).
    void keyDown();
    void keyUp();

    // Voice source. startVoice() switches the ISR to the ring buffer;
    // stream samples with queueVoice() (returns how many were accepted;
    // retry the rest when the ring has space). When the file is done,
    // wait for voiceDrained() then call endVoice(). abortVoice() stops
    // immediately and discards queued samples.
    //
    // A constant pilot tone (pilotLevel > 0) is mixed in during voice
    // mode to keep the SA868 mic AGC loaded through speech pauses; it
    // sits below the radio's TX high-pass so it never reaches the air.
    void startVoice(uint32_t sampleRateHz, int32_t gain,
                    uint32_t pilotHz = 0, int32_t pilotLevel = 0);
    size_t queueVoice(const int16_t *samples, size_t count);
    bool voiceDrained() const;
    void endVoice();
    void abortVoice();
};
