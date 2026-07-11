// LilyFox - morse code fox-hunt beacon for the LilyGO T-TWR Plus V2.0.
//
// Transmits foxconfig::MESSAGE in morse code for TX_WINDOW_SECONDS at the
// start of every PERIOD_SECONDS cycle. All settings live in
// include/fox_config.h.

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <esp_system.h>

#include "board_pins.h"
#include "board_power.h"
#include "fox_config.h"
#include "fox_display.h"
#include "morse_sender.h"
#include "sa868_radio.h"
#include "toggle_button.h"
#include "tone_output.h"

// Runs the transmit/silence cycle: keys the radio, repeats the message
// as many complete times as fit the TX window, then idles out the period.
// A click of the rotary encoder toggles the beacon on/off; the OLED
// shows "Beacon Active" / "Beacon Inactive".
class FoxController
{
public:
    // Brings up the PMU, display, radio, and tone generator. Halts
    // (blinking red) if the hardware does not respond.
    void begin();

    // Handles the toggle button and runs beacon cycles; call repeatedly
    // from loop().
    void service();

private:
    void _runBeaconCycle();
    void _startTransmission();
    void _stopTransmission();
    void _showStatus(uint8_t red, uint8_t green, uint8_t blue);
    void _haltWithError(const char *reason);

    BoardPower power_;
    Sa868Radio radio_{Serial1};
    ToneOutput tone_;
    MorseSender morse_{tone_, foxconfig::WORDS_PER_MINUTE};
    Adafruit_NeoPixel status_led_{1, boardpins::NEOPIXEL, NEO_GRB + NEO_KHZ800};
    FoxDisplay display_;
    ToggleButton button_;
    bool beacon_enabled_ = foxconfig::START_ENABLED;
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
    // NeoPixel, so nothing is visible until it is enabled.
    const bool pmu_online = power_.begin();

    // Blue LED as early as possible: proves the application is running
    // even when no serial monitor is attached.
    status_led_.begin();
    status_led_.setBrightness(25);
    _showStatus(0, 0, 255); // blue = initializing

    Serial.begin(115200);
    // USB CDC re-enumerates after every reset; give a monitor time to
    // reattach so boot logs are not lost (the monitor no longer resets
    // the board now that DTR/RTS are left alone).
    delay(3000);
    Serial.println("LilyFox morse fox beacon starting");
    Serial.printf("Last reset: %s\n", resetReasonName(esp_reset_reason()));

    if (!pmu_online)
        _haltWithError("AXP2101 PMU not responding on I2C");
    Serial.println("PMU online, radio rail enabled");
    power_.printStatus();

    if (display_.begin())
        Serial.println("OLED display found");
    else
        Serial.println("No OLED display found; running headless");

    // The SA868 is sometimes slow to come up; keep power-cycling and
    // re-probing until it answers rather than giving up. A firmware
    // mismatch, however, will never fix itself.
    for (int bring_up_round = 1;; bring_up_round++)
    {
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

    bool frequency_set = false;
    for (int attempt = 1; attempt <= 3 && !frequency_set; attempt++)
        frequency_set = radio_.setFrequency(foxconfig::TX_FREQUENCY_MHZ);
    if (!frequency_set)
        _haltWithError("SA868 rejected AT+DMOSETGROUP (frequency out of band?)");
    Serial.printf("Frequency set to %.4f MHz\n", foxconfig::TX_FREQUENCY_MHZ);

    tone_.begin(boardpins::ESP_TO_MIC, foxconfig::TONE_HZ);

    const uint32_t message_ms = morse_.durationMs(foxconfig::MESSAGE);
    Serial.printf("Message \"%s\" at %u WPM takes %.1f s; window %u s of every %u s\n",
                  foxconfig::MESSAGE, foxconfig::WORDS_PER_MINUTE,
                  message_ms / 1000.0, foxconfig::TX_WINDOW_SECONDS,
                  foxconfig::PERIOD_SECONDS);
    if (message_ms > foxconfig::TX_WINDOW_SECONDS * 1000UL)
        Serial.println("NOTE: message is longer than the TX window; "
                       "each cycle sends it once and overruns the window");

    button_.begin(boardpins::ENCODER_CLICK);
    display_.showStatus(beacon_enabled_);
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
        display_.showStatus(beacon_enabled_);
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
    const uint32_t message_ms = morse_.durationMs(foxconfig::MESSAGE);
    const MorseSender::AbortPredicate button_pressed = [this]() {
        return button_.pressPending();
    };

    cycle_number_++;
    Serial.printf("Cycle %lu: transmitting\n", static_cast<unsigned long>(cycle_number_));
    _startTransmission();

    uint32_t repetitions = 0;
    bool aborted = false;
    while (!aborted)
    {
        if (button_.pressPending())
            break; // handled at the top of service()
        const uint32_t elapsed_ms = millis() - cycle_start_ms;
        const bool message_fits = (elapsed_ms + message_ms <= window_ms);
        if (repetitions > 0 && !message_fits)
            break; // never cut a message short; always send at least one
        aborted = !morse_.send(foxconfig::MESSAGE, button_pressed);
        if (!aborted)
            repetitions++;
    }

    _stopTransmission();
    Serial.printf("Cycle %lu: sent %lu repetition(s), TX %.1f s%s\n",
                  static_cast<unsigned long>(cycle_number_),
                  static_cast<unsigned long>(repetitions),
                  (millis() - cycle_start_ms) / 1000.0,
                  aborted ? " (stopped by button)" : "");

    while (millis() - cycle_start_ms < period_ms)
    {
        if (button_.pressPending())
            return; // let service() toggle without waiting out the period
        delay(20);
    }
}

void FoxController::_startTransmission()
{
    _showStatus(255, 0, 0); // red = on the air
    tone_.enable();
    radio_.pttOn(foxconfig::USE_HIGH_POWER);
    delay(foxconfig::PTT_LEAD_MS);
}

void FoxController::_stopTransmission()
{
    delay(foxconfig::PTT_TAIL_MS);
    radio_.pttOff();
    tone_.disable();
    _showStatus(0, 32, 0); // dim green = idle
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
