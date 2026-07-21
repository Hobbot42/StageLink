#include "ServoOutput.h"

#include <Arduino.h>
#include <ESP32Servo.h>
#include "ConfigManager.h"

// StageLink ServoOutput

namespace
{
    // Fallback values used until these keys are ever written - preserves
    // today's hardcoded behavior on a board with nothing stored yet.
    constexpr int DEFAULT_SERVO_MIN_ANGLE = 0;
    constexpr int DEFAULT_SERVO_MAX_ANGLE = 180;

    int servoMinAngle = DEFAULT_SERVO_MIN_ANGLE;
    int servoMaxAngle = DEFAULT_SERVO_MAX_ANGLE;

    Servo servo;
}

void ServoOutput::begin(uint8_t pin)
{
    servoMinAngle = StageLink::ConfigManager::getInt("servoMin", DEFAULT_SERVO_MIN_ANGLE);
    servoMaxAngle = StageLink::ConfigManager::getInt("servoMax", DEFAULT_SERVO_MAX_ANGLE);

    servo.attach(pin);
}

void ServoOutput::setAngle(int angle)
{
    // Clamp defensively - callers may pass a value decoded from a radio
    // packet, which this code doesn't otherwise validate.
    servo.write(constrain(angle, servoMinAngle, servoMaxAngle));
}
