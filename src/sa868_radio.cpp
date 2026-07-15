#include "sa868_radio.h"

#include "board_pins.h"

namespace
{
constexpr uint32_t UART_BAUD = 9600;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 1000;
} // namespace

Sa868Radio::Sa868Radio(HardwareSerial &serial) : serial_{serial}
{
}

void Sa868Radio::parkPinsForPowerOff()
{
    serial_.end(); // stop driving the module's UART RX line

    const int8_t radio_side_pins[] = {
        boardpins::SA868_PTT,     boardpins::SA868_HIGH_LOW,
        boardpins::MIC_SELECT,    boardpins::SA868_POWER_DOWN,
        boardpins::SA868_UART_TX, boardpins::ESP_TO_MIC,
    };
    for (const int8_t pin : radio_side_pins)
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    pinMode(boardpins::SA868_UART_RX, INPUT); // no pull-up back-feed
}

Sa868Radio::ProbeResult Sa868Radio::begin()
{
    pinMode(boardpins::SA868_PTT, OUTPUT);
    digitalWrite(boardpins::SA868_PTT, HIGH); // PTT idle (active LOW)

    _setPowerPin(false); // low RF power until keyed otherwise

    pinMode(boardpins::MIC_SELECT, OUTPUT);
    digitalWrite(boardpins::MIC_SELECT, HIGH); // route ESP audio to mic input

    // Power-cycle the module so it boots cleanly on the now-enabled rail.
    pinMode(boardpins::SA868_POWER_DOWN, OUTPUT);
    digitalWrite(boardpins::SA868_POWER_DOWN, LOW);
    delay(500);
    digitalWrite(boardpins::SA868_POWER_DOWN, HIGH);
    delay(2000);

    serial_.begin(UART_BAUD, SERIAL_8N1,
                  boardpins::SA868_UART_RX, boardpins::SA868_UART_TX);

    // Some firmwares print a banner at boot; show whatever arrives.
    _dumpBootChatter(2000);

    // The datasheet specifies 9600 baud only; probing other rates never
    // succeeded in practice and just slowed the retry rounds down.
    const ProbeResult result = _probeAtCurrentBaud();
    if (result == ProbeResult::STOCK_OK)
    {
        // Classic SA868 vs SA868S: only the S answers AT+MODEL.
        // Their DMOSETGROUP first fields differ (bandwidth vs TX
        // power), so configure() needs to know which one this is.
        String model;
        s_series_ = _command("AT+MODEL", model, RESPONSE_TIMEOUT_MS) &&
                    model.indexOf("SA868S") >= 0;
        Serial.printf("Module variant: %s\n",
                      s_series_ ? "SA868S (S-series)" : "classic SA868");
    }
    return result;
}

void Sa868Radio::_dumpBootChatter(uint32_t listenMs)
{
    String chatter;
    const uint32_t start_ms = millis();
    while (millis() - start_ms < listenMs)
    {
        while (serial_.available() > 0)
            chatter += static_cast<char>(serial_.read());
        delay(5);
    }
    if (chatter.length() == 0)
    {
        Serial.println("No boot chatter from SA868 (silent power-up is normal for stock firmware)");
        return;
    }
    Serial.printf("SA868 boot chatter (%u bytes):", chatter.length());
    for (unsigned index = 0; index < chatter.length(); index++)
    {
        const uint8_t byte_value = static_cast<uint8_t>(chatter[index]);
        Serial.printf(" %02X", byte_value);
    }
    Serial.print("  ascii: '");
    for (unsigned index = 0; index < chatter.length(); index++)
    {
        const char character = chatter[index];
        Serial.print(isprint(static_cast<unsigned char>(character)) ? character : '.');
    }
    Serial.println("'");
}

Sa868Radio::ProbeResult Sa868Radio::_probeAtCurrentBaud()
{
    String response;
    for (int attempt = 1; attempt <= 4; attempt++)
    {
        if (_command("AT+DMOCONNECT", response, RESPONSE_TIMEOUT_MS))
            return ProbeResult::STOCK_OK;
        Serial.printf("AT+DMOCONNECT attempt %d: no response\n", attempt);
    }
    if (_command("AT+VERSION", response, RESPONSE_TIMEOUT_MS) &&
        response.indexOf("sa8x8") >= 0)
    {
        return ProbeResult::OPENRTX_FIRMWARE;
    }
    if (_command("AT+MODEL", response, RESPONSE_TIMEOUT_MS) &&
        response.indexOf("SA868S") >= 0)
    {
        return ProbeResult::SA868S_FIRMWARE;
    }
    return ProbeResult::NO_RESPONSE;
}

bool Sa868Radio::configure(bool highPower, double frequencyMhz, uint8_t squelchLevel)
{
    // AT+DMOSETGROUP=<first>,TFV,RFV,Tx_CXCSS,SQ,Rx_CXCSS with
    // "0000" = no CTCSS/CDCSS. The first field differs by module
    // generation (detected via AT+MODEL in begin()):
    //   SA868S (v1.7 datasheet): TX power, 0 = high, 1 = low
    //   classic SA868 (v1.2):    bandwidth, 0 = 12.5 kHz, 1 = 25 kHz
    //                            (power is set by the H/L pin only)
    const int first_field = s_series_ ? (highPower ? 0 : 1) : 0;
    char group_command[80];
    snprintf(group_command, sizeof(group_command),
             "AT+DMOSETGROUP=%d,%.4f,%.4f,0000,%u,0000",
             first_field, frequencyMhz, frequencyMhz, squelchLevel);

    String response;
    const bool group_ok = _command(group_command, response, 2000);
    if (!group_ok)
        return false;

    // Disable the CTCSS squelch-tail-eliminator burst at unkey. Not in
    // the SA868/SA868S datasheets, but documented for the sibling SA818
    // and acknowledged by this module ("+DMOSETTAIL:0"); moot with
    // CTCSS off, kept as insurance against a tail burst on the air.
    //
    // Bypass pre-emphasis and high-pass (first two SETFILTER fields):
    // with pre-emphasis OFF, the receiver's de-emphasis rolls off the
    // mic-path AGC hiss by 6 dB/octave instead of hearing it flat, and
    // the voice gets the classic mellow comms sound. (Measured A/B
    // against the LilyGO factory firmware, which bypasses all three;
    // the earlier 0,0,0 setting made voice and hiss arrive spectrally
    // flat and harsh.)
    _command("AT+SETTAIL=0", response, RESPONSE_TIMEOUT_MS);
    _command("AT+SETFILTER=1,1,1", response, RESPONSE_TIMEOUT_MS);

    return true;
}

void Sa868Radio::pttOn(bool highPower)
{
    _setPowerPin(highPower);
    digitalWrite(boardpins::SA868_PTT, LOW);
}

void Sa868Radio::pttOff()
{
    digitalWrite(boardpins::SA868_PTT, HIGH);
    _setPowerPin(false);
}

void Sa868Radio::_setPowerPin(bool highPower)
{
    if (highPower)
    {
        pinMode(boardpins::SA868_HIGH_LOW, INPUT); // float = high power
    }
    else
    {
        pinMode(boardpins::SA868_HIGH_LOW, OUTPUT);
        digitalWrite(boardpins::SA868_HIGH_LOW, LOW);
    }
}

bool Sa868Radio::_command(const char *command, String &response, uint32_t timeoutMs)
{
    response = "";
    while (serial_.available() > 0)
        serial_.read(); // drop stale bytes

    serial_.print(command);
    serial_.print("\r\n");

    const uint32_t start_ms = millis();
    while (millis() - start_ms < timeoutMs)
    {
        while (serial_.available() > 0)
        {
            response += static_cast<char>(serial_.read());
            if (response.endsWith("\r\n"))
            {
                Serial.printf("SA868 '%s' -> '%s'\n", command, response.c_str());
                return true;
            }
        }
        delay(5);
    }
    if (response.length() > 0)
        Serial.printf("SA868 '%s' timed out with partial reply '%s'\n",
                      command, response.c_str());
    return false;
}
