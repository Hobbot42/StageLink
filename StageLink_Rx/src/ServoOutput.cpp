#include "ServoOutput.h"

#include <Arduino.h>
#include "ConfigManager.h"

namespace
{
    // Fallback values used until these keys are ever written - preserves
    // today's hardcoded behavior on a board with nothing stored yet.
    constexpr int DEFAULT_SERVO_MIN_ANGLE = 0;
    constexpr int DEFAULT_SERVO_MAX_ANGLE = 180;

    // How often (and how far) tick() steps toward the target angle - 5
    // degrees every 5ms (up to 1000 deg/sec), so a full 0-180 sweep
    // takes ~0.18s. Fast enough that a hobby servo's own mechanical
    // slew rate is the real limit, not this step rate - responsiveness
    // (keeping up with a fast knob turn) prioritized over the smoothing
    // this was originally added for; some jitter on quick moves is the
    // accepted tradeoff.
    constexpr unsigned long SERVO_STEP_INTERVAL_MS = 5;
    constexpr int SERVO_STEP_SIZE = 5;
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

    if (millis() - lastStepTime < SERVO_STEP_INTERVAL_MS)
    {
        return;
    }

    lastStepTime = millis();

    int delta = targetAngle - currentAngle;
    int step = constrain(delta, -SERVO_STEP_SIZE, SERVO_STEP_SIZE);
    currentAngle += step;

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
