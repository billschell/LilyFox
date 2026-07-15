// LilyFox - morse code fox-hunt beacon for the LilyGO T-TWR Plus V2.0.
//
// Transmits foxconfig::MESSAGE in morse code for TX_WINDOW_SECONDS at the
// start of every PERIOD_SECONDS cycle. All settings live in
// include/fox_config.h.

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <esp_system.h>

#include "audio_output.h"
#include "board_pins.h"
#include "board_power.h"
#include "fox_config.h"
#include "fox_display.h"
#include "morse_sender.h"
#include "sa868_radio.h"
#include "toggle_button.h"
#include "voice_player.h"
#include "voice_playlist.h"

// Runs the transmit/silence cycle: keys the radio, transmits (voice
// recordings from the SD card in rotation followed by a morse ID, or a
// morse-only beacon when no card is present), then idles out the
// period. A click of the rotary encoder toggles the beacon on/off; the
// OLED shows the beacon state and what is happening right now.
class FoxController
{
public:
    // Brings up the PMU, display, radio, SD card, and audio output.
    // Halts (blinking red) if the radio hardware does not respond.
    void begin();

    // Handles the toggle button and runs beacon cycles; call repeatedly
    // from loop().
    void service();

private:
    void _runBeaconCycle();
    // Loads the next playable recording into PSRAM (SD access must
    // finish before PTT keys). Falls back to morse-only when none load.
    void _loadNextRecording();
    // The loaded recording plus the morse ID; false if aborted by button.
    bool _transmitVoiceAndId();
    uint32_t _estimateNextTransmissionMs() const;
    void _startTransmission();
    void _stopTransmission();
    void _showActivity(const char *activity);
    void _showStatus(uint8_t red, uint8_t green, uint8_t blue);
    void _haltWithError(const char *reason);

    BoardPower power_;
    Sa868Radio radio_{Serial1};
    AudioOutput audio_;
    MorseSender morse_{audio_, foxconfig::WORDS_PER_MINUTE};
    VoicePlaylist playlist_;
    VoicePlayer voice_{audio_};
    Adafruit_NeoPixel status_led_{1, boardpins::NEOPIXEL, NEO_GRB + NEO_KHZ800};
    FoxDisplay display_;
    ToggleButton button_;
    bool beacon_enabled_ = foxconfig::START_ENABLED;
    bool voice_mode_ = false;
    String current_recording_;
    uint32_t cycle_number_ = 0;
};

FoxController fox;

// Human-readable reason for the last chip reset; a run of BROWNOUT
// resets points at power problems (flat battery, sagging supply).
static const char *resetReasonName(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software reset";
    case ESP_RST_PANIC:     return "crash (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "BROWNOUT - supply voltage sagged";
    case ESP_RST_USB:       return "USB reset";
    default:                return "other";
    }
}

void setup()
{
    fox.begin();
}

void loop()
{
    fox.service();
}

void FoxController::begin()
{
    // PMU first: on Rev 2.0 the DC3 rail powers both the radio and the
    // NeoPixel, and it brings up the I2C bus the display needs.
    const bool pmu_online = power_.begin();

    // Blue LED as early as possible: proves the application is running
    // even when no serial monitor is attached.
    status_led_.begin();
    status_led_.setBrightness(25);
    _showStatus(0, 0, 255); // blue = initializing

    // Display immediately after: the SH1106 shows random power-on RAM
    // until the first frame is drawn, so paint a boot screen before the
    // slow parts of startup (serial grace period, SA868 bring-up).
    const bool display_found = display_.begin();
    display_.showBoot("Initializing (1)");

    Serial.begin(115200);
    // USB CDC re-enumerates after every reset; give a monitor time to
    // reattach so boot logs are not lost (the monitor no longer resets
    // the board now that DTR/RTS are left alone).
    delay(3000);
    Serial.printf("%s LilyFox fox beacon starting\n", foxconfig::CALLSIGN);
    Serial.printf("Last reset: %s\n", resetReasonName(esp_reset_reason()));

    if (!pmu_online)
        _haltWithError("AXP2101 PMU not responding on I2C");
    Serial.println("PMU online, radio rail enabled");
    power_.printStatus();

    if (display_found)
        Serial.println("OLED display found");
    else
        Serial.println("No OLED display found; running headless");

    // The SA868 is sometimes slow to come up; keep power-cycling and
    // re-probing until it answers rather than giving up. A firmware
    // mismatch, however, will never fix itself.
    for (int bring_up_round = 1;; bring_up_round++)
    {
        char boot_status[22];
        snprintf(boot_status, sizeof(boot_status), "Initializing (%d)", bring_up_round);
        display_.showBoot(boot_status);

        // Always start the module from a true cold rail: an ESP reset
        // (USB monitor reconnect, flashing, brownout) mid-transmission
        // leaves the SA868 wedged, and it survives both PD-pin toggling
        // and a rail cycle if any ESP pin keeps back-powering it.
        radio_.parkPinsForPowerOff();
        power_.cycleRadioRail();
        const Sa868Radio::ProbeResult probe = radio_.begin();
        if (probe == Sa868Radio::ProbeResult::STOCK_OK)
        {
            Serial.println("SA868 online (stock NiceRF firmware)");
            break;
        }
        if (probe == Sa868Radio::ProbeResult::OPENRTX_FIRMWARE)
            _haltWithError("SA868 is running the OpenRTX sa8x8 firmware, not "
                           "stock NiceRF - this build only speaks the stock "
                           "AT protocol");
        if (probe == Sa868Radio::ProbeResult::SA868S_FIRMWARE)
            _haltWithError("module is an SA868S (S-series firmware) - it "
                           "uses a different AT command set than the classic "
                           "SA868");

        Serial.printf("SA868 silent in bring-up round %d; power-cycling and "
                      "retrying\n", bring_up_round);
        power_.printStatus();
        for (int blink = 0; blink < 3; blink++) // blink red, keep trying
        {
            _showStatus(255, 0, 0);
            delay(250);
            _showStatus(0, 0, 0);
            delay(250);
        }
    }

    bool radio_configured = false;
    for (int attempt = 1; attempt <= 3 && !radio_configured; attempt++)
        radio_configured = radio_.configure(foxconfig::USE_HIGH_POWER,
                                            foxconfig::FREQUENCY_MHZ,
                                            foxconfig::SQUELCH_LEVEL);
    if (!radio_configured)
        _haltWithError("SA868 rejected AT+DMOSETGROUP (frequency out of band?)");
    Serial.printf("Radio configured: %.4f MHz simplex, %s power, squelch %u, "
                  "no CTCSS\n", foxconfig::FREQUENCY_MHZ,
                  foxconfig::USE_HIGH_POWER ? "high" : "low",
                  foxconfig::SQUELCH_LEVEL);

    audio_.begin(boardpins::ESP_TO_MIC, foxconfig::TONE_HZ);

    if (foxconfig::VOICE_ENABLED && playlist_.begin() && playlist_.count() > 0)
    {
        voice_mode_ = true;
        Serial.printf("Voice mode: rotating through %u recording(s), "
                      "morse ID \"%s\"\n", playlist_.count(), foxconfig::MORSE_ID);
        Serial.printf("PSRAM free: %u bytes\n", ESP.getFreePsram());
    }
    else
    {
        Serial.println("Morse-only beacon (voice disabled or no recordings)");
    }

    const uint32_t message_ms = morse_.durationMs(foxconfig::MESSAGE);
    Serial.printf("Morse message \"%s\" at %u WPM takes %.1f s; window %u s of every %u s\n",
                  foxconfig::MESSAGE, foxconfig::WORDS_PER_MINUTE,
                  message_ms / 1000.0, foxconfig::TX_WINDOW_SECONDS,
                  foxconfig::PERIOD_SECONDS);

    button_.begin(boardpins::ENCODER_CLICK);
    _showActivity(voice_mode_ ? "voice + morse ID" : "morse beacon");
    Serial.printf("Beacon %s (click the encoder to toggle)\n",
                  beacon_enabled_ ? "active" : "inactive");

    if (beacon_enabled_)
        _showStatus(0, 32, 0); // dim green = ready
    else
        _showStatus(0, 0, 0);
}

void FoxController::service()
{
    if (button_.consumePress())
    {
        beacon_enabled_ = !beacon_enabled_;
        Serial.printf("Beacon %s\n", beacon_enabled_ ? "active" : "inactive");
        _showActivity(beacon_enabled_ ? "starting" : "click knob to start");
        _showStatus(0, beacon_enabled_ ? 32 : 0, 0);
    }
    if (!beacon_enabled_)
    {
        delay(20);
        return;
    }
    _runBeaconCycle();
}

void FoxController::_runBeaconCycle()
{
    const uint32_t cycle_start_ms = millis();
    const uint32_t window_ms = foxconfig::TX_WINDOW_SECONDS * 1000UL;
    const uint32_t period_ms = foxconfig::PERIOD_SECONDS * 1000UL;
    const MorseSender::AbortPredicate button_pressed = [this]() {
        return button_.pressPending();
    };

    cycle_number_++;
    if (voice_mode_)
        _loadNextRecording(); // all SD access happens before PTT keys
    Serial.printf("Cycle %lu: transmitting\n", static_cast<unsigned long>(cycle_number_));
    _startTransmission();

    uint32_t transmissions = 0;
    bool aborted = false;
    while (!aborted)
    {
        if (button_.pressPending())
            break; // handled at the top of service()
        const uint32_t elapsed_ms = millis() - cycle_start_ms;
        const bool fits = (elapsed_ms + _estimateNextTransmissionMs() <= window_ms);
        if (transmissions > 0 && !fits)
            break; // only complete transmissions; always at least one
        if (voice_mode_)
        {
            aborted = !_transmitVoiceAndId();
        }
        else
        {
            _showActivity("morse beacon");
            aborted = !morse_.send(foxconfig::MESSAGE, button_pressed);
        }
        if (!aborted)
            transmissions++;
    }

    _stopTransmission();
    voice_.unload();
    Serial.printf("Cycle %lu: %lu transmission(s), TX %.1f s%s\n",
                  static_cast<unsigned long>(cycle_number_),
                  static_cast<unsigned long>(transmissions),
                  (millis() - cycle_start_ms) / 1000.0,
                  aborted ? " (stopped by button)" : "");

    uint32_t shown_remaining_s = UINT32_MAX;
    while (millis() - cycle_start_ms < period_ms)
    {
        if (button_.pressPending())
            return; // let service() toggle without waiting out the period
        const uint32_t remaining_ms = period_ms - (millis() - cycle_start_ms);
        const uint32_t remaining_s = (remaining_ms + 999) / 1000;
        if (remaining_s != shown_remaining_s)
        {
            shown_remaining_s = remaining_s;
            char activity[22];
            snprintf(activity, sizeof(activity), "next TX in %lus",
                     static_cast<unsigned long>(remaining_s));
            _showActivity(activity);
        }
        delay(20);
    }
}

void FoxController::_loadNextRecording()
{
    // Try each recording once, in rotation order, until one loads. The
    // rotation advances one file per cycle even when loading succeeds
    // on the first try, so cycles walk through the playlist.
    for (size_t attempt = 0; attempt < playlist_.count(); attempt++)
    {
        const String path = playlist_.peekNext();
        playlist_.advance();
        if (voice_.load(path.c_str()))
        {
            current_recording_ = path;
            return;
        }
        Serial.printf("Skipping unplayable %s\n", path.c_str());
    }
    Serial.println("No playable recordings; morse-only fallback");
    voice_mode_ = false;
}

bool FoxController::_transmitVoiceAndId()
{
    const MorseSender::AbortPredicate button_pressed = [this]() {
        return button_.pressPending();
    };

    char activity[22];
    snprintf(activity, sizeof(activity), "voice: %s",
             current_recording_.c_str() + 1); // skip the '/'
    _showActivity(activity);

    if (!voice_.play(button_pressed))
        return false; // aborted by the operator (nothing loaded cannot occur)

    delay(foxconfig::VOICE_ID_GAP_MS);
    if (button_.pressPending())
        return false;

    snprintf(activity, sizeof(activity), "morse: %s", foxconfig::MORSE_ID);
    _showActivity(activity);
    return morse_.send(foxconfig::MORSE_ID, button_pressed);
}

uint32_t FoxController::_estimateNextTransmissionMs() const
{
    if (!voice_mode_)
        return morse_.durationMs(foxconfig::MESSAGE);
    return voice_.durationMs() + foxconfig::VOICE_ID_GAP_MS +
           morse_.durationMs(foxconfig::MORSE_ID);
}

void FoxController::_startTransmission()
{
    _showStatus(255, 0, 0); // red = on the air
    audio_.enable();
    radio_.pttOn(foxconfig::USE_HIGH_POWER);
    delay(foxconfig::PTT_LEAD_MS);
}

void FoxController::_stopTransmission()
{
    delay(foxconfig::PTT_TAIL_MS);
    radio_.pttOff();
    audio_.disable();
    _showStatus(0, 32, 0); // dim green = idle
}

void FoxController::_showActivity(const char *activity)
{
    display_.show(beacon_enabled_, activity);
}

void FoxController::_showStatus(uint8_t red, uint8_t green, uint8_t blue)
{
    status_led_.setPixelColor(0, status_led_.Color(red, green, blue));
    status_led_.show();
}

void FoxController::_haltWithError(const char *reason)
{
    Serial.printf("FATAL: %s\n", reason);
    while (true)
    {
        _showStatus(255, 0, 0);
        delay(250);
        _showStatus(0, 0, 0);
        delay(250);
    }
}
