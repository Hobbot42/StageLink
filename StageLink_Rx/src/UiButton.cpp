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
            pressEvent_ = true;
        }
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
