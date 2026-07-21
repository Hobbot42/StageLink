#include "StatusLED.h"

#include <Adafruit_NeoPixel.h>

// StageLink StatusLED

namespace
{
    constexpr uint8_t LED_PIN = 2;
    constexpr uint8_t LED_COUNT = 1;

    Adafruit_NeoPixel led(
        LED_COUNT,
        LED_PIN,
        NEO_GRB + NEO_KHZ800
    );
}


void StatusLED::begin()
{
    led.begin();
    led.setBrightness(30);
    led.clear();
    led.show();
}


void StatusLED::setReady()
{
    led.setPixelColor(
        0,
        led.Color(0, 255, 0)
    );

    led.show();
}


void StatusLED::setOffline()
{
    led.setPixelColor(
        0,
        led.Color(255, 0, 0)
    );

    led.show();
}


void StatusLED::setConfig()
{
    led.setPixelColor(
        0,
        led.Color(0, 0, 255)
    );

    led.show();
}