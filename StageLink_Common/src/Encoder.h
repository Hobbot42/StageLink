// StageLink Encoder
// Rotary encoder (quadrature) + integrated push-button input, used by TX
// as the operator's control input. Rotation is decoded via GPIO
// interrupts for responsiveness; the button is polled and debounced in
// update().
// Belongs to: StageLink_Common, but currently only wired up on TX.

#pragma once

#include <Arduino.h>

class Encoder
{
public:
    static void begin(
        uint8_t clockPin,
        uint8_t dataPin,
        uint8_t buttonPin
    );

    // Must be called every loop() - polls and debounces the button (rotation
    // is handled separately by interrupt, not by this call).
    static void update();

    // Absolute position accumulated since begin(); never resets on its own.
    static int position();

    // Drains the net step count (+/-) accumulated since the last call, for
    // callers that only care about "did it just turn, and which way."
    static int consumeTurn();

    // One-shot "was pressed since last check" - for edge-triggered actions
    // like cue triggers, distinct from isButtonPressed()'s live state.
    static bool buttonPressed();

    // One-shot state-change notification (either direction), used when a
    // caller needs to know about releases too, not just presses.
    static bool consumeButtonStateChange(bool &pressed);

    static bool isButtonPressed();

private:
    // Runs on every CLK/DT edge; keep this minimal and ISR-safe.
    static void IRAM_ATTR handleRotationInterrupt();
};
