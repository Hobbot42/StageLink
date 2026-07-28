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

    // One-shot "was tapped since the last check". Reported on *release*,
    // not on the press edge, because a press can't be told apart from the
    // start of a hold until the button comes back up. A press that turned
    // into a hold never reports here - consumeHold() got it instead.
    bool consumePress();

    // One-shot "has been held past HOLD_MS", reported once while the
    // button is still down rather than on release, so the action happens
    // when the operator feels the hold land.
    bool consumeHold();

private:
    // Matches the legacy pages' LOCAL_BUTTON_HOLD_MS (see main.cpp), so
    // a hold feels the same everywhere on the controller.
    static constexpr unsigned long HOLD_MS = 600;

    uint8_t pin_;
    bool lastRawState_ = false;
    bool debouncedState_ = false;
    unsigned long lastChangeTime_ = 0;
    unsigned long pressStartTime_ = 0;
    bool pressEvent_ = false;
    bool holdEvent_ = false;
    bool holdFired_ = false;
};
