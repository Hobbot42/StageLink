#include "LEDOutput.h"

#include <Arduino.h>
#include "ConfigManager.h"

namespace
{
    // Fallback values used until these keys are ever written - a fresh
    // board works out of the box without any stored configuration.
    constexpr uint8_t DEFAULT_LED_PIN = 4;
    constexpr int32_t DEFAULT_LED_COUNT = 8;
    constexpr uint8_t DEFAULT_LED_BRIGHTNESS = 128;
}

bool LEDOutput::begin()
{
    ledPin = StageLink::ConfigManager::getUInt8("ledPin", DEFAULT_LED_PIN);
    ledCount = static_cast<uint16_t>(
        StageLink::ConfigManager::getInt("ledCount", DEFAULT_LED_COUNT)
    );
    uint8_t startupBrightness = StageLink::ConfigManager::getUInt8(
        "ledBrightness",
        DEFAULT_LED_BRIGHTNESS
    );

    strip.setPin(ledPin);
    strip.updateLength(ledCount);

    bool success = strip.begin();

    // All pixels share one color (white) - brightness alone drives the
    // visible output for this first version, see update().
    strip.fill(strip.Color(255, 255, 255));
    strip.setBrightness(startupBrightness);
    strip.show();

    return success;
}

void LEDOutput::update(int32_t value)
{
    strip.setBrightness(constrain(static_cast<int>(value), 0, 255));
    strip.show();
}

void LEDOutput::diagnostics()
{
    // Placeholder - nothing to report yet.
}
