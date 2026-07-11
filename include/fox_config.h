// LilyFox user configuration.
//
// Edit these values and reflash (pio run -t upload) to change behavior.

#pragma once

#include <stdint.h>

namespace foxconfig
{

// Text sent in morse code. Case is ignored (morse has no case).
// Supported characters: A-Z, 0-9, space, and . , ? / = -
inline constexpr char MESSAGE[] = "fox fox fox fox de W2WZ";

// Morse speed in words per minute (PARIS standard).
// Note: at 10 WPM the default message above takes ~29.6 s to send.
inline constexpr uint32_t WORDS_PER_MINUTE = 10;

// Transmission schedule: transmit for TX_WINDOW_SECONDS at the start of
// every PERIOD_SECONDS cycle.  Only complete messages are sent - a
// repetition starts only if it fits in the remaining window, so the
// transmission may end early rather than cut off mid-callsign.  At least
// one full message is always sent per cycle, even if it overruns the
// window.
inline constexpr uint32_t TX_WINDOW_SECONDS = 30;
inline constexpr uint32_t PERIOD_SECONDS = 60;

// Transmit frequency in MHz.  146.5650 is the common US 2 m fox-hunt
// frequency.  Must be within your SA868 module's band (VHF: 134-174 MHz).
inline constexpr double TX_FREQUENCY_MHZ = 146.5650;

// Audio tone frequency in Hz (typical fox/CW tones: 600-1000).
inline constexpr uint32_t TONE_HZ = 800;

// RF power: false = low (~0.5 W), true = high (~1 W).
inline constexpr bool USE_HIGH_POWER = false;

// Whether the beacon starts transmitting right after boot. Either way,
// a click of the rotary encoder toggles it on/off at any time.
inline constexpr bool START_ENABLED = true;

// --- Voice playback (micro-SD card) ---

// When true and .wav files are found on the SD card, each transmission
// plays the next recording in alphabetical rotation, followed by
// MORSE_ID. Without a card (or with this false) the beacon sends
// MESSAGE in morse as before.
inline constexpr bool VOICE_ENABLED = true;

// Morse identification appended after each voice recording.
inline constexpr char MORSE_ID[] = "de W2WZ";

// Silence between the end of the recording and the morse ID.
inline constexpr uint32_t VOICE_ID_GAP_MS = 400;

// Playback level: 170 maps a full-scale WAV sample to the maximum
// clean mic drive (same ceiling the morse tone uses). Lower if a hot
// recording sounds distorted on the air; raise (up to ~255) if a quiet
// one is weak. Normalizing the recording to about -3 dB is better than
// pushing this past 170.
inline constexpr int32_t VOICE_GAIN = 170;

// Milliseconds to hold PTT before the first tone (transmitter settle
// time) and after the last tone before unkeying.
inline constexpr uint32_t PTT_LEAD_MS = 500;
inline constexpr uint32_t PTT_TAIL_MS = 200;

} // namespace foxconfig
