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

    currentAngle = constrain(currentAngle, servoMinAngle, servoMaxAngle);
    targetAngle = currentAngle;

    // attach() returns the assigned channel number, or 0 on failure (e.g.
    // no free PWM channel).
    bool attached = servo.attach(pin) != 0;
    if (attached)
    {
        // Written twice deliberately: on this hardware, the very first
        // write() after attach() is silently dropped (ESP32 LEDC duty
        // register not yet latched) - without this, the servo's actual
        // first commanded move would eat that dropped write and appear
        // to do nothing, only starting to respond from the second
        // command onward. Both writes use the same (harmless, already
        // home) angle, so there's no visible motion either way - this
        // just spends the "lost" write here instead of on a real cue.
        servo.write(currentAngle);
        servo.write(currentAngle);
    }

    return attached;
}

void ServoOutput::update(int32_t value)
{
    // Clamp defensively - callers may pass a value decoded from a radio
    // packet, which this code doesn't otherwise validate. Only sets the
    // target - tick() eases the physical servo toward it.
    targetAngle = constrain(static_cast<int>(value), servoMinAngle, servoMaxAngle);
}

void ServoOutput::tick()
{
    if (currentAngle == targetAngle)
    {
        return;
    }

    // Commands the angle as given, with no rate limiting of its own - the
    // servo's own mechanical slew rate is then the only limit. This used
    // to step 5 degrees every 5ms, which put a fixed ~0.18s ramp under
    // every move and meant a zero-second cue never truly snapped. A move
    // that should take time gets it from the cue's fade instead (see
    // ActionEngine.h), so timing lives in one place.
    currentAngle = targetAngle;

    servo.write(currentAngle);
}

void ServoOutput::diagnostics()
{
    // Placeholder - nothing to report yet.
}

void ServoOutput::refreshState()
{
    servo.write(currentAngle);
}
