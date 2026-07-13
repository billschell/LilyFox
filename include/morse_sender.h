#pragma once

#include <stdint.h>
#include <functional>

#include "audio_output.h"

// Converts text to morse code and sends it by keying an AudioOutput tone.
// Timing follows the PARIS standard: dit = 1200 / WPM milliseconds,
// dah = 3 dits, inter-element gap = 1 dit, inter-character gap = 3 dits,
// word gap = 7 dits.
class MorseSender
{
public:
    // Checked between morse elements; return true to stop sending.
    using AbortPredicate = std::function<bool()>;

    MorseSender(AudioOutput &tone, uint32_t wordsPerMinute);

    // Sends the whole message, blocking until done. Characters with no
    // morse mapping are skipped. If abort is provided and returns true,
    // sending stops between elements (tone off) and send returns false;
    // returns true when the full message was sent.
    bool send(const char *text, const AbortPredicate &abort = nullptr) const;

    // Exact on-air duration of send() for this text, in milliseconds.
    uint32_t durationMs(const char *text) const;

private:
    // Returns the dit/dah pattern for a character (e.g. "-.-.") or
    // nullptr if the character has no morse mapping.
    static const char *_lookupPattern(char character);

    // Returns false if aborted mid-pattern.
    bool _sendPattern(const char *pattern, const AbortPredicate &abort) const;

    AudioOutput &tone_;
    uint32_t dit_ms_;
};
