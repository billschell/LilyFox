#include "fox_display.h"

#include <Wire.h>

#include "fox_config.h"

namespace
{

// Returns the I2C address the OLED answers on, or 0 if absent.
uint8_t findOledAddress()
{
    const uint8_t candidates[] = {0x3C, 0x3D};
    for (const uint8_t address : candidates)
    {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0)
            return address;
    }
    return 0;
}

} // namespace

bool FoxDisplay::begin()
{
    const uint8_t address = findOledAddress();
    if (address == 0)
        return false;

    // reset=false: no reset pin wired. The library's Wire.begin() call
    // is a no-op because the PMU already initialized the bus on 8/9.
    available_ = oled_.begin(address, false);
    return available_;
}

void FoxDisplay::showBoot(const char *status)
{
    if (!available_)
        return;

    oled_.clearDisplay();
    oled_.setTextColor(SH110X_WHITE);

    oled_.setTextSize(1);
    oled_.setCursor(0, 0);
    oled_.printf("%s LilyFox %.3f", foxconfig::CALLSIGN, foxconfig::FREQUENCY_MHZ);

    oled_.setTextSize(1);
    oled_.setCursor(0, 32);
    oled_.print(status);

    oled_.display();
}

void FoxDisplay::show(bool beaconActive, const char *activity)
{
    if (!available_)
        return;

    oled_.clearDisplay();
    oled_.setTextColor(SH110X_WHITE);

    oled_.setTextSize(1);
    oled_.setCursor(0, 0);
    oled_.printf("%s LilyFox %.3f", foxconfig::CALLSIGN, foxconfig::FREQUENCY_MHZ);

    oled_.setTextSize(2);
    oled_.setCursor(0, 18);
    oled_.print("Beacon");
    oled_.setCursor(0, 36);
    oled_.print(beaconActive ? "Active" : "Inactive");

    oled_.setTextSize(1);
    oled_.setCursor(0, 56);
    char line[22]; // the display fits 21 characters per line
    snprintf(line, sizeof(line), "%s", activity);
    oled_.print(line);

    oled_.display();
}
