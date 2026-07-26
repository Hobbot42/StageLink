// StageLink_Rx UiButton
// Minimal debounced digital input for RX's two dedicated GUI buttons
// (Back on GPIO26, Action on GPIO27 - see main.cpp). Separate from
// Encoder's own built-in button, and from StageLink_Tx's Button class
// (a single hardcoded-pin static singleton - not reusable here since RX
// needs two independently-pinned instances).
// Belongs to: StageLink_Rx.

#pragma once

#include <cstdint>

class UiButton
{
public:
    explicit UiButton(uint8_t pin);

    // Configures the pin as INPUT_PULLUP - same active-low convention
    // (pressed = pulled to GND) as Encoder's button and TX's Button.
    void begin();

    // Must be called every loop() - polls and debounces.
    void update();

    // One-shot "was pressed since the last check" - edge-triggered,
    // same convention as Encoder::consumeButtonStateChange()'s press
    // edge, but this class only reports the press (not release), since
    // that's all GuiController currently needs from these two buttons.
    bool consumePress();

private:
    uint8_t pin_;
    bool lastRawState_ = false;
    bool debouncedState_ = false;
    unsigned long lastChangeTime_ = 0;
    bool pressEvent_ = false;
};
