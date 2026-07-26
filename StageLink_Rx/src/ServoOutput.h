// StageLink ServoOutput
// Thin wrapper around ESP32Servo driving RX's physical servo output.
// Registered with OutputManager (see RX main.cpp) rather than called
// directly - angle arrives from TX via VALUE_UPDATE (live) and
// STATE_SNAPSHOT (resync on reconnect), both routed through OutputManager.
// update() only records the target angle; tick() (called every loop(),
// see RX main.cpp) eases the servo toward it a degree at a time instead
// of snapping instantly, same fade-style approach LEDOutput uses.
// Belongs to: StageLink_Rx.

#pragma once

#include <ESP32Servo.h>
#include "OutputDevice.h"

class ServoOutput : public StageLink::OutputDevice
{
public:
    explicit ServoOutput(uint8_t pin);

    bool begin() override;

    // Sets the target angle - the physical servo doesn't move until
    // tick() eases it there. See the class comment.
    void update(int32_t value) override;

    void diagnostics() override;

    // Reapplies the current (already-eased) angle - see
    // OutputDevice::refreshState().
    void refreshState() override;

    // Call every loop(). Rate-limited internally - a no-op most calls,
    // only actually steps (and writes to the servo) once per
    // SERVO_STEP_INTERVAL_MS. Same easing approach as LEDOutput::tick().
    void tick();

private:
    uint8_t pin;
    int servoMinAngle;
    int servoMaxAngle;
    Servo servo;

    // Currently written angle vs. where tick() is easing it toward -
    // smooths what would otherwise be an instant snap to each new
    // commanded angle (every VALUE_UPDATE/STATE_SNAPSHOT is a discrete
    // step from the encoder).
    int currentAngle = 0;
    int targetAngle = 0;
    unsigned long lastStepTime = 0;
};
