#include "board_power.h"

#include <Wire.h>

#include "board_pins.h"

bool BoardPower::begin()
{
    bool pmuOnline = pmu_.begin(Wire, AXP2101_SLAVE_ADDRESS,
                                boardpins::PMU_I2C_SDA, boardpins::PMU_I2C_SCL);
    if (!pmuOnline)
    {
        return false;
    }

    // DC3 powers the SA868 radio and the NeoPixel. 3.4 V per LilyGO design.
    pmu_.setDC3Voltage(3400);
    pmu_.enableDC3();

    // ALDO2 powers the micro-SD card slot.
    pmu_.setALDO2Voltage(3300);
    pmu_.enableALDO2();

    // The battery pack has no thermistor; without this the charger faults.
    pmu_.disableTSPinMeasure();

    return true;
}

void BoardPower::cycleRadioRail()
{
    // Callers must park the radio-side GPIOs low first (see
    // Sa868Radio::parkPinsForPowerOff), otherwise the module is
    // back-powered through its I/O pins and never truly restarts.
    Serial.println("Power-cycling the radio rail (DC3)");
    pmu_.disableDC3();
    delay(1000); // let the module's supply caps fully discharge
    pmu_.enableDC3();
    delay(200); // rail settle time before the module starts booting
}

void BoardPower::printStatus()
{
    Serial.printf("PMU DC3 (radio rail): %s at %u mV\n",
                  pmu_.isEnableDC3() ? "ON" : "OFF", pmu_.getDC3Voltage());
    Serial.printf("PMU battery: %s, %u mV; USB power: %s\n",
                  pmu_.isBatteryConnect() ? "connected" : "absent",
                  pmu_.getBattVoltage(),
                  pmu_.isVbusIn() ? "present" : "absent");
}
