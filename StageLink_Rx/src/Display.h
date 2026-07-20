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
        int localEncoderValue
    );

    static void showDiagnostics(
        const char *linkState,
        bool rssiAvailable,
        int8_t rssi,
        uint32_t packetCount,
        uint32_t retryErrorCount
    );
};
