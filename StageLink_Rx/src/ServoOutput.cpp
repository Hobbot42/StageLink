#include "ServoOutput.h"

#include <Arduino.h>
#include "ConfigManager.h"

namespace
{
    // Fallback values used until these keys are ever written - preserves
    // today's hardcoded behavior on a board with nothing stored yet.
    constexpr int DEFAULT_SERVO_MIN_ANGLE = 0;
    constexpr int DEFAULT_SERVO_MAX_ANGLE = 180;
}

ServoOutput::ServoOutput(uint8_t pin)
    : pin(pin),
      servoMinAngle(DEFAULT_SERVO_MIN_ANGLE),
      servoMaxAngle(DEFAULT_SERVO_MAX_ANGLE)
{
}

bool ServoOutput::begin()
{
    servoMinAngle = StageLink::ConfigManager::getInt("servoMin", DEFAULT_SERVO_MIN_ANGLE);
    servoMaxAngle = StageLink::ConfigManager::getInt("servoMax", DEFAULT_SERVO_MAX_ANGLE);

    // attach() returns the assigned channel number, or 0 on failure (e.g.
    // no free PWM channel).
    return servo.attach(pin) != 0;
}

void ServoOutput::update(int32_t value)
{
    // Clamp defensively - callers may pass a value decoded from a radio
    // packet, which this code doesn't otherwise validate.
    servo.write(constrain(static_cast<int>(value), servoMinAngle, servoMaxAngle));
}

void ServoOutput::diagnostics()
{
    // Placeholder - nothing to report yet.
}
