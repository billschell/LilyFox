#include "morse_sender.h"

#include <Arduino.h>
#include <ctype.h>

namespace
{

constexpr uint32_t DIT_UNITS = 1;
constexpr uint32_t DAH_UNITS = 3;
constexpr uint32_t ELEMENT_GAP_UNITS = 1;
constexpr uint32_t CHARACTER_GAP_UNITS = 3;
constexpr uint32_t WORD_GAP_UNITS = 7;

struct MorseMapping
{
    char character;
    const char *pattern;
};

constexpr MorseMapping MORSE_TABLE[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
    {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
    {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
    {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
    {'8', "---.."}, {'9', "----."},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."},
    {'/', "-..-."},  {'=', "-...-"},  {'-', "-....-"},
};

} // namespace

MorseSender::MorseSender(ToneOutput &tone, uint32_t wordsPerMinute)
    : tone_{tone}, dit_ms_{1200 / wordsPerMinute}
{
}

bool MorseSender::send(const char *text, const AbortPredicate &abort) const
{
    uint32_t pending_gap_units = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++)
    {
        if (*cursor == ' ')
        {
            if (pending_gap_units > 0)
                pending_gap_units = WORD_GAP_UNITS;
            continue;
        }
        const char *pattern = _lookupPattern(*cursor);
        if (pattern == nullptr)
            continue;
        if (abort && abort())
            return false;
        if (pending_gap_units > 0)
            delay(pending_gap_units * dit_ms_);
        if (!_sendPattern(pattern, abort))
            return false;
        pending_gap_units = CHARACTER_GAP_UNITS;
    }
    return true;
}

const char *MorseSender::_lookupPattern(char character)
{
    const char upper = static_cast<char>(toupper(static_cast<unsigned char>(character)));
    for (const MorseMapping &mapping : MORSE_TABLE)
    {
        if (mapping.character == upper)
            return mapping.pattern;
    }
    return nullptr;
}

bool MorseSender::_sendPattern(const char *pattern, const AbortPredicate &abort) const
{
    for (const char *element = pattern; *element != '\0'; element++)
    {
        if (abort && abort())
            return false;
        if (element != pattern)
            delay(ELEMENT_GAP_UNITS * dit_ms_);
        const uint32_t element_units = (*element == '-') ? DAH_UNITS : DIT_UNITS;
        tone_.keyDown();
        delay(element_units * dit_ms_);
        tone_.keyUp();
    }
    return true;
}

uint32_t MorseSender::durationMs(const char *text) const
{
    uint32_t total_units = 0;
    uint32_t pending_gap_units = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++)
    {
        if (*cursor == ' ')
        {
            if (pending_gap_units > 0)
                pending_gap_units = WORD_GAP_UNITS;
            continue;
        }
        const char *pattern = _lookupPattern(*cursor);
        if (pattern == nullptr)
            continue;
        total_units += pending_gap_units;
        for (const char *element = pattern; *element != '\0'; element++)
        {
            if (element != pattern)
                total_units += ELEMENT_GAP_UNITS;
            total_units += (*element == '-') ? DAH_UNITS : DIT_UNITS;
        }
        pending_gap_units = CHARACTER_GAP_UNITS;
    }
    return total_units * dit_ms_;
}
