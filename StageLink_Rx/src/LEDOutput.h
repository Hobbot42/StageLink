// StageLink LEDOutput
// Drives one addressable LED strip as a single dimmable light - every
// pixel set to the same color, controlled by overall brightness only.
// Registered with OutputManager (see RX main.cpp) rather than driven
// directly, same as ServoOutput.
//
// Deliberately minimal for this first version: no per-pixel/RGB control,
// effects, or WLED/DMX passthrough yet - see update() for the value
// convention. Those are meant to come later, either as new methods here
// or as new OutputDevice implementations registered on their own channels.
// Belongs to: StageLink_Rx.

#pragma once

#include <Adafruit_NeoPixel.h>
#include "OutputDevice.h"

class LEDOutput : public StageLink::OutputDevice
{
public:
    bool begin() override;

    // value is 0-255: 0 = off, 255 = full brightness. Clamped if out of range.
    void update(int32_t value) override;

    void diagnostics() override;

private:
    uint8_t ledPin = 0;
    uint16_t ledCount = 0;
    Adafruit_NeoPixel strip;
};
