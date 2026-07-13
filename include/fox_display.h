#pragma once

#include <Adafruit_SH110X.h>

// The T-TWR Plus's 128x64 OLED, used as a simple status panel. The
// panel is an SH1106 (not SSD1306: the SH1106 lacks the horizontal
// addressing mode, which shows up as random pixels if driven with an
// SSD1306 driver). The display is optional: if none is found on the
// I2C bus, every call becomes a no-op so the fox still runs headless.
class FoxDisplay
{
public:
    // Probes the OLED (address 0x3C on VHF units, 0x3D on UHF) on the
    // already-initialized I2C bus. Returns false if no display answers.
    bool begin();

    // Shows "Beacon Active" / "Beacon Inactive" plus a one-line activity
    // description of what the fox is doing right now (e.g. the file
    // being played, "morse: de W2WZ", "next TX in 23s").
    void show(bool beaconActive, const char *activity);

private:
    Adafruit_SH1106G oled_{128, 64, &Wire, -1};
    bool available_ = false;
};
