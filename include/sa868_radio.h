#pragma once

#include <Arduino.h>

// Controls an SA868 module running the stock NiceRF firmware:
// configuration over UART AT commands, PTT / power level / power-down
// via GPIO.
class Sa868Radio
{
public:
    enum class ProbeResult
    {
        STOCK_OK,         // answered AT+DMOCONNECT (stock NiceRF protocol)
        OPENRTX_FIRMWARE, // answered AT+VERSION with an sa8x8-fw banner
        SA868S_FIRMWARE,  // answered AT+MODEL with SA868S-... (newer
                          // NiceRF S-series command set)
        NO_RESPONSE,      // nothing on the UART at all
    };

    explicit Sa868Radio(HardwareSerial &serial);

    // Releases the UART and drives every radio-side pin LOW so the module
    // is not back-powered through its I/O protection diodes while its
    // supply rail is switched off. Call before cutting the rail;
    // begin() restores everything.
    void parkPinsForPowerOff();

    // Sets up the control GPIOs, powers the module up, and identifies it:
    // dumps any boot chatter, then probes the stock, sa8x8-fw, and
    // SA868S command sets at both 9600 and 115200 baud. All UART traffic
    // is echoed to the USB serial console.
    ProbeResult begin();

    // Programs simplex frequency (MHz), 12.5 kHz bandwidth, no CTCSS.
    bool setFrequency(double frequencyMhz);

    // Keys the transmitter. The high/low power pin is set first.
    void pttOn(bool highPower);
    void pttOff();

private:
    // Sends one AT command (without line ending) and waits for a
    // CR/LF-terminated response.
    bool _command(const char *command, String &response, uint32_t timeoutMs);

    // Collects UART bytes for the given time and hex-dumps anything seen.
    void _dumpBootChatter(uint32_t listenMs);

    ProbeResult _probeAtCurrentBaud();

    HardwareSerial &serial_;
};
