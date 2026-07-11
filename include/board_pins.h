// LilyGO T-TWR Plus V2.0 pin assignments (ESP32-S3 <-> SA868 and PMU).
// Values taken from the working ESP32APRS_T-TWR project for this board.

#pragma once

#include <stdint.h>

namespace boardpins
{

// SA868 radio module
inline constexpr int8_t SA868_UART_TX = 39; // ESP32 TX -> SA868 RX
inline constexpr int8_t SA868_UART_RX = 48; // ESP32 RX <- SA868 TX
inline constexpr int8_t SA868_PTT = 41;     // active LOW keys the transmitter
inline constexpr int8_t SA868_POWER_DOWN = 40; // HIGH = module powered up
inline constexpr int8_t SA868_HIGH_LOW = 38;   // HIGH = high RF power
inline constexpr int8_t MIC_SELECT = 17;       // HIGH = ESP audio, LOW = onboard mic
inline constexpr int8_t ESP_TO_MIC = 18;       // sigma-delta audio out -> SA868 mic in

// AXP2101 power management unit (I2C)
inline constexpr int8_t PMU_I2C_SDA = 8;
inline constexpr int8_t PMU_I2C_SCL = 9;

// Status LED (WS2812 NeoPixel)
inline constexpr int8_t NEOPIXEL = 42;

// Rotary encoder push button (to GND, needs pull-up)
inline constexpr int8_t ENCODER_CLICK = 21;

// Micro-SD (TF) card slot on the SPI bus, powered by the PMU's ALDO2
inline constexpr int8_t SD_SPI_SCK = 12;
inline constexpr int8_t SD_SPI_MISO = 13;
inline constexpr int8_t SD_SPI_MOSI = 11;
inline constexpr int8_t SD_CS = 10;

} // namespace boardpins
