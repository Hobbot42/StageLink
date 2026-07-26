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

    // --- FxQ GUI Architecture Prototype v0.1 ---
    // Generic screens reused across many menu levels (Show Menu, Edit
    // Show, Edit Cue, Output Select, Command Select, Setup Menu) - see
    // StageLink_Rx/src/GuiController.h. Fake-data navigation testing
    // only, no real hardware/effect behavior.

    // Scrollable "> "-cursor list. contextLabel is an optional second
    // header line (e.g. the show name on Edit Show) - pass nullptr to
    // omit it and gain one more visible row.
    static void showGuiList(
        const char *title,
        const char *contextLabel,
        const char *const *items,
        uint8_t itemCount,
        uint8_t selectedIndex
    );

    // SHOW MODE home screen - device label, show name, and a large-font
    // current/next cue readout from ShowEngine (see GuiController.cpp):
    // "QNN Name" for current, ">QNN Name" for next (see
    // formatCueLabel()). No previous-cue line and no small "CURRENT"/
    // "NEXT" text headers - dropped so both lines could be shown large
    // (the display's real estate at this font doesn't allow headers,
    // 2 large cue lines, and the signal bar all at once); the ">"
    // prefix is what distinguishes next from current. A null or empty
    // *CueName omits the name entirely (just "QNN", no trailing space).
    // The bottom row is either a graphical RSSI signal-strength bar
    // (rssiAvailable/rssi, same source as the legacy Diagnostics page)
    // or "LINK LOST" text when linkOnline is false - automatically
    // reverts to the bar once the link comes back, since this just
    // reflects whatever render() passes in on the next call.
    static void showGuiShowRun(
        const char *deviceLabel,
        const char *showName,
        uint8_t currentCue,
        const char *currentCueName,
        uint8_t nextCue,
        const char *nextCueName,
        bool linkOnline,
        bool rssiAvailable,
        int8_t rssi
    );

    // One field at a time out of fieldCount (LED/Servo/Stepper action
    // values, and Cue Time) - the value is passed pre-formatted so this
    // stays agnostic to int vs. float fields.
    static void showGuiValueEntry(
        const char *title,
        const char *contextLabel,
        const char *fieldLabel,
        const char *valueText,
        uint8_t fieldIndex,
        uint8_t fieldCount
    );

    // See UpdateMode.h. ipAddress is nullptr/empty until WiFi actually
    // connects; secondsRemaining < 0 hides the countdown (used once a
    // flash is in progress, since UpdateMode stops enforcing the
    // no-connection timeout at that point).
    static void showUpdateMode(
        const char *statusLine,
        const char *ipAddress,
        int secondsRemaining
    );
};
