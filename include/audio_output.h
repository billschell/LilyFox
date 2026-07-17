#pragma once

#include <stddef.h>
#include <stdint.h>

// Generates transmit audio on the SA868 mic input using the ESP32-S3
// I2S peripheral in PDM TX mode: DMA-fed 16-bit samples through the
// hardware PDM modulator, no per-sample interrupts.
//
// Two sample sources:
//  - Tone: a keyed sine for morse (keyDown / keyUp), rendered in
//    blocks by a small task.
//  - Voice: 16-bit PCM written directly into the DMA queue at the
//    file's native sample rate (the I2S clock is reconfigured per
//    recording, so no resampling is needed).
//
// Both sources pass through a ~5 ms amplitude envelope so keying and
// playback start/stop are click-free.
//
// Only one instance may exist: the implementation uses file-scope state.
class AudioOutput
{
public:
    // Creates the I2S PDM TX channel and the tone-render task. The
    // channel is left disabled; call enable() before keying or playing.
    void begin(int8_t outputPin, uint32_t toneHz);

    // Start/stop audio output. Enable only while transmitting so the
    // line is quiet between cycles.
    void enable();
    void disable();

    // Tone source (morse keying).
    void keyDown();
    void keyUp();

    // Voice source. startVoice() reclocks the channel to the file's
    // rate; stream samples with queueVoice() (accepts a bounded amount
    // per call, blocking until the DMA takes it - loop and check for
    // aborts between calls). When the file is done, wait for
    // voiceDrained() then call endVoice(). abortVoice() stops
    // immediately and discards queued samples.
    void startVoice(uint32_t sampleRateHz, int32_t gain);
    size_t queueVoice(const int16_t *samples, size_t count);
    // Call once after the last queueVoice() so the underrun statistics
    // exclude the expected zero-fill tail while the ring drains.
    void feedComplete();
    bool voiceDrained() const;
    void endVoice();
    void abortVoice();
};
