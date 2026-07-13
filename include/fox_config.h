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
inline constexpr uint32_t WORDS_PER_MINUTE = 20;

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

// Loudness-normalize each recording when it is loaded: a fixed gain
// brings speech up to VOICE_TARGET_RMS and a soft limiter keeps the
// peaks clean. The SA868's mic AGC amplifies its own hiss whenever the
// input is quiet, so keeping the average drive high keeps the AGC
// gain - and its noise - wound down.
// inline constexpr bool VOICE_NORMALIZE = true;
inline constexpr bool VOICE_NORMALIZE = false;



// Target average (RMS) speech level after normalization, as a fraction
// of full scale. 0.20 = -14 dBFS: hot enough to keep the AGC loaded
// while leaving the limiter mostly idle on speech peaks.
inline constexpr float VOICE_TARGET_RMS = 0.20f;

// Low-pass the recording at load time (0 disables). Only needed when
// the radio's TX pre-emphasis is enabled (SETFILTER=0,0,0), where
// boosted sibilance above ~3 kHz overdeviates. With the filters
// bypassed (1,1,1, the factory configuration) the receiver's
// de-emphasis already rolls off the top end - a digital low-pass on
// top of that sounds mushy.
inline constexpr uint32_t VOICE_LOWPASS_HZ = 3400;


// Cap the length of silent pauses inside recordings when they load.
// The SA868 mic AGC takes ~300 ms (measured) to wind its gain - and
// its amplified hiss - back up after speech stops; pauses shorter than
// that stay as quiet as morse inter-element gaps. Long pauses are
// trimmed to VOICE_MAX_PAUSE_MS so the hiss never fully returns.
inline constexpr bool VOICE_TRIM_PAUSES = true;
inline constexpr uint32_t VOICE_MAX_PAUSE_MS = 250;

// Pilot tone experiment (kept for reference): a constant low tone was
// meant to hold the AGC down and be stripped by the TX high-pass, but
// the SA868 transmits it at full volume - the mic-path high-pass does
// not remove it. Leave disabled.
inline constexpr bool VOICE_PILOT_ENABLED = false;
inline constexpr uint32_t VOICE_PILOT_HZ = 150;
inline constexpr int32_t VOICE_PILOT_LEVEL = 20;


// Milliseconds to hold PTT before the first tone (transmitter settle
// time) and after the last tone before unkeying.
inline constexpr uint32_t PTT_LEAD_MS = 500;
inline constexpr uint32_t PTT_TAIL_MS = 200;

} // namespace foxconfig
