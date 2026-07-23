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

    // Normal (non-editing) view of the unit label page - see
    // showUnitLabelEdit() for the in-progress editing view.
    static void showUnitLabel(const char *unitLabel);

    // buffer is the in-progress edit value (may not be saved yet, and is
    // space-padded to StageLink::LABEL_MAX_LENGTH), cursor is which
    // character rotating the encoder currently changes - see
    // StageLink_Common/src/LabelEditor.h.
    static void showUnitLabelEdit(const char *buffer, uint8_t cursor);

    // Proof-of-concept page for EffectEngine/EffectStorage - shows
    // whether the test effect is currently playing, and whether it came
    // from a previous save (storedAtBoot) or had to be generated fresh
    // because nothing was saved yet (see RX main.cpp's
    // effectLoadedFromStorage, set once at boot).
    static void showEffectTest(bool running, bool storedAtBoot);

    // Shows the most recent trigger source (e.g. "Cue", "Local Button")
    // and the effect number assigned to it - see RX main.cpp's
    // fireTrigger()/TriggerManager.h. effectNumber is 1-based to match
    // "Effect 1" elsewhere; 0 means no trigger has fired since boot.
    static void showTriggerStatus(const char *lastTriggerLabel, int effectNumber);
};
