// FxQ
// Product: TxQ
// Version: v0.9.0
//
// Project information is maintained in FxQInfo.h
//
// TxQ Display
// Drives the TX unit's OLED. Shows TX's own encoder/button/servo state
// on the status page, ReliableRadio's diagnostics on another, and RX's
// self-reported device info (see DeviceInfo.h) on a third.
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
        const char *controlModeLabel,
        int controlModeValue
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

    static void showDeviceInfo(
        bool infoReceived,
        const char *deviceName,
        const char *firmwareVersion,
        const char *capabilitiesLine
    );
};