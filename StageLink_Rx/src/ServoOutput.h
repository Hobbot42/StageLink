// StageLink ServoOutput
// Thin wrapper around ESP32Servo driving RX's physical servo output.
// Registered with OutputManager (see RX main.cpp) rather than called
// directly - angle arrives from TX via VALUE_UPDATE (live) and
// STATE_SNAPSHOT (resync on reconnect), both routed through OutputManager.
// Belongs to: StageLink_Rx.

#pragma once

#include <ESP32Servo.h>
#include "OutputDevice.h"

class ServoOutput : public StageLink::OutputDevice
{
public:
    explicit ServoOutput(uint8_t pin);

    bool begin() override;

    void update(int32_t value) override;

    void diagnostics() override;

private:
    uint8_t pin;
    int servoMinAngle;
    int servoMaxAngle;
    Servo servo;
};
