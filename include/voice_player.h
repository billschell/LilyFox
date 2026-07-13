#pragma once

#include <stddef.h>
#include <stdint.h>
#include <functional>

#include "audio_output.h"

// Plays SD-card recordings through the AudioOutput voice path.
//
// The whole recording is loaded into PSRAM with load() BEFORE the
// transmitter is keyed: SD/SPI bus activity couples audibly into the
// mic audio line, so no card access may happen while on the air.
class VoicePlayer
{
public:
    // Checked while streaming; return true to stop playback.
    using AbortPredicate = std::function<bool()>;

    explicit VoicePlayer(AudioOutput &audio);

    // Reads the whole file into PSRAM. Returns false if the file is
    // unreadable or memory is exhausted. Frees any previous recording.
    bool load(const char *path);
    void unload();
    bool loaded() const;
    uint32_t durationMs() const;

    // Plays the loaded recording, blocking; no SD access. Returns false
    // if nothing is loaded or playback was aborted by the predicate.
    bool play(const AbortPredicate &abort = nullptr);

private:
    // Band-limits the recording to VOICE_LOWPASS_HZ so the radio's TX
    // pre-emphasis cannot push sibilance past the deviation limit.
    void _lowPass();

    // Applies a fixed makeup gain toward VOICE_TARGET_RMS with a soft
    // limiter, in place, at load time (keeps the SA868 mic AGC loaded).
    void _normalizeLoudness();

    // Shortens silent pauses to VOICE_MAX_PAUSE_MS, in place, so the
    // mic AGC's hiss never has time to wind back up between phrases.
    void _trimPauses();

    AudioOutput &audio_;
    int16_t *samples_ = nullptr;
    size_t sample_count_ = 0;
    uint32_t sample_rate_ = 0;
};
