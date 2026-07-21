#pragma once

#include <cstdint>

// StageLink ConfigManager
// Generic persisted key/value settings on top of ESP32 Preferences (NVS).
// The only storage layer used - no direct Preferences access outside this
// class, same pattern as ReliableRadio wrapping esp_now_*.
// Belongs to: StageLink_Common (shared by TX and RX).

namespace StageLink
{
    class ConfigManager
    {
    public:
        static void begin();

        static uint8_t getUInt8(const char *key, uint8_t defaultValue);
        static void putUInt8(const char *key, uint8_t value);

        static int32_t getInt(const char *key, int32_t defaultValue);
        static void putInt(const char *key, int32_t value);

        static float getFloat(const char *key, float defaultValue);
        static void putFloat(const char *key, float value);

        static bool getBool(const char *key, bool defaultValue);
        static void putBool(const char *key, bool value);

        // Erases all stored settings for this board. Callers fall back to
        // their own hardcoded defaults on the next read afterward.
        static void resetToDefaults();
    };
}
