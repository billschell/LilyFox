#pragma once

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

// Brings up the T-TWR Plus power rails via the AXP2101 PMU.
// The SA868 radio is powered from the PMU's DC3 rail (3.4 V), so the
// radio is dead until this runs.
class BoardPower
{
public:
    // Initializes the PMU over I2C and enables the radio rail.
    // Returns false if the PMU does not respond.
    bool begin();

    // Prints rail status (DC3 radio rail, battery, USB) to Serial.
    void printStatus();

    // Drops the radio's DC3 rail and re-enables it: a true power cycle.
    // Toggling the SA868's PD (sleep) pin is not enough to recover the
    // module when it was keyed mid-transmission by an ESP reset.
    void cycleRadioRail();

private:
    XPowersAXP2101 pmu_;
};
