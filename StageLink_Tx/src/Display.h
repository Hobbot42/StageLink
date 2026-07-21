// StageLink Display (TX)
// Drives the TX unit's OLED. Shows TX's own encoder/button/servo state
// on the status page, and ReliableRadio's diagnostics on the other.
// Belongs to: StageLink_Tx (StageLink_Rx has its own Display with a
// different set of pages - not shared, since TX and RX show different
// information).

#pragma once

#include <cstdint>

class Display
{
public:
    static bool begin();

    static void showReady();

    static void showConfigMode();

    static void showStatus(
        const char *linkState,
        int encoderValue,
        bool buttonPressed,
        int servoAngle
    );

    static void showDiagnostics(
        const char *linkState,
        unsigned long lastLatencyMs,
        unsigned long averageLatencyMs,
        unsigned long maxLatencyMs,
        uint8_t retryCount,
        uint32_t failedCount,
        bool rssiAvailable,
        int8_t rssi
    );
};