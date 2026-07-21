// StageLink ServoOutput
// Thin wrapper around ESP32Servo driving RX's physical servo output.
// Angle arrives from TX via VALUE_UPDATE (live) and STATE_SNAPSHOT
// (resync on reconnect) - see RX main.cpp.
// Belongs to: StageLink_Rx.

#pragma once

#include <cstdint>

class ServoOutput
{
public:
    static void begin(uint8_t pin);

    static void setAngle(int angle);
};
