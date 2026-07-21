// StageLink Display (RX)
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

    static void showDiagnostics(
        const char *linkState,
        bool rssiAvailable,
        int8_t rssi,
        uint32_t packetCount,
        uint32_t retryErrorCount
    );
};
