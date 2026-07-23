// FxQ
// Product: RxQ
// Version: v0.9.0
//
// Project information is maintained in FxQInfo.h
//
// RxQ Display
// Drives the RX unit's OLED. The main status page (showLinkAndEncoder)
// shows both TX's remote state and RX's own resulting state (servo
// angle) side by side; the other page shows ReliableRadio's diagnostics.
// showEncoderValue/showButtonState/showLinkState are single-field pages
// kept for standalone use/debugging - normal operation uses
// showLinkAndEncoder instead.
// Belongs to: StageLink_Rx (StageLink_Tx has its own Display - not
// shared, since TX and RX show different information).

#pragma once

#include <cstdint>
#include "DeviceInfo.h"

class Display
{
public:
    static bool begin();

    static void showEncoderValue(int value);

    static void showButtonState(bool pressed);

    static void showLinkState(const char *state);

    static void showLinkAndEncoder(
        const char *linkState,
        int encoderValue,
        bool buttonPressed,
        int localEncoderValue,
        int servoAngle
    );

    // deviceId is RX's own identity (see localDeviceInfo in main.cpp) -
    // passed through rather than read from a second copy, same source
    // used for the Serial startup banner and TX's OLED.
    static void showDiagnostics(
        const char *linkState,
        bool rssiAvailable,
        int8_t rssi,
        uint32_t packetCount,
        uint32_t retryErrorCount,
        const uint8_t deviceId[StageLink::DEVICE_ID_LENGTH]
    );

    // Normal (non-editing) view of the unit name page - see
    // showUnitNameEdit() for the in-progress editing view.
    static void showUnitName(const char *unitName);

    // char0/char1 are the in-progress edit buffer (may not be saved
    // yet), editingCharIndex (0 or 1) is which one rotating the encoder
    // currently changes - see RX main.cpp's UnitEditState.
    static void showUnitNameEdit(char char0, char char1, uint8_t editingCharIndex);

    // Proof-of-concept page for EffectEngine (see EffectEngine.h) -
    // shows whether the hardcoded test effect is currently playing.
    static void showEffectTest(bool running);
};
