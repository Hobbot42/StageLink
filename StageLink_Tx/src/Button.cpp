#include "Button.h"

#include <Arduino.h>

// StageLink Button

namespace
{
    constexpr uint8_t BUTTON_PIN = 0;
}


void Button::begin()
{
    pinMode(
        BUTTON_PIN,
        INPUT_PULLUP
    );
}


bool Button::pressed()
{
    return digitalRead(BUTTON_PIN) == LOW;
}