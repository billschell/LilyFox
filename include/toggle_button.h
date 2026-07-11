#pragma once

#include <stdint.h>

// Debounced push button on a GPIO (active low, internal pull-up),
// sampled by a falling-edge interrupt so a press is never missed even
// while the main loop is busy sending morse.
//
// Only one instance may exist: the ISR uses file-scope state.
class ToggleButton
{
public:
    void begin(int8_t pin);

    // True if a press has been registered and not yet consumed.
    // Safe to call from tight loops.
    bool pressPending() const;

    // Returns true once per registered press and clears it.
    bool consumePress();
};
