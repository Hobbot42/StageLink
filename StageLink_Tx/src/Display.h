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
        bool buttonPressed
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