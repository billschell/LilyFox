#include "toggle_button.h"

#include <Arduino.h>

namespace
{

constexpr uint32_t DEBOUNCE_MS = 300;

volatile bool press_pending = false;
volatile uint32_t last_press_ms = 0;

void IRAM_ATTR on_falling_edge()
{
    const uint32_t now_ms = millis();
    if (now_ms - last_press_ms >= DEBOUNCE_MS)
    {
        last_press_ms = now_ms;
        press_pending = true;
    }
}

} // namespace

void ToggleButton::begin(int8_t pin)
{
    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin), on_falling_edge, FALLING);
}

bool ToggleButton::pressPending() const
{
    return press_pending;
}

bool ToggleButton::consumePress()
{
    const bool was_pending = press_pending;
    press_pending = false;
    return was_pending;
}
