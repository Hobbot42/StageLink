#pragma once

#include <Arduino.h>

class Encoder
{
public:
    static void begin(
        uint8_t clockPin,
        uint8_t dataPin,
        uint8_t buttonPin
    );

    static void update();

    static int position();

    static int consumeTurn();

    static bool buttonPressed();

private:
    static void IRAM_ATTR handleRotationInterrupt();
};
