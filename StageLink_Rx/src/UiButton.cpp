#include "UiButton.h"

#include <Arduino.h>

namespace
{
    constexpr unsigned long DEBOUNCE_MS = 30;
}

UiButton::UiButton(uint8_t pin) : pin_(pin)
{
}

void UiButton::begin()
{
    pinMode(pin_, INPUT_PULLUP);
}

void UiButton::update()
{
    bool raw = digitalRead(pin_) == LOW;

    if (raw != lastRawState_)
    {
        lastChangeTime_ = millis();
        lastRawState_ = raw;
    }

    if (debouncedState_ != lastRawState_ && millis() - lastChangeTime_ >= DEBOUNCE_MS)
    {
        debouncedState_ = lastRawState_;

        if (debouncedState_)
        {
            pressStartTime_ = millis();
            holdFired_ = false;
        }
        else if (!holdFired_)
        {
            // Released before the hold threshold, so this was a tap. A
            // release that ends a hold reports nothing - the hold already
            // did the work, and firing both would run two actions from
            // one gesture.
            pressEvent_ = true;
        }
    }

    if (debouncedState_ && !holdFired_ && millis() - pressStartTime_ >= HOLD_MS)
    {
        holdFired_ = true;
        holdEvent_ = true;
    }
}

bool UiButton::consumePress()
{
    if (pressEvent_)
    {
        pressEvent_ = false;
        return true;
    }

    return false;
}

bool UiButton::consumeHold()
{
    if (holdEvent_)
    {
        holdEvent_ = false;
        return true;
    }

    return false;
}
