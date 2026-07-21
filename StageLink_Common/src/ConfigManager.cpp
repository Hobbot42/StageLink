#include "ConfigManager.h"

#include <Preferences.h>

namespace
{
    constexpr const char *NAMESPACE = "stagelink";

    Preferences preferences;
}

void StageLink::ConfigManager::begin()
{
    preferences.begin(NAMESPACE, false);
}

uint8_t StageLink::ConfigManager::getUInt8(const char *key, uint8_t defaultValue)
{
    return preferences.getUChar(key, defaultValue);
}

void StageLink::ConfigManager::putUInt8(const char *key, uint8_t value)
{
    preferences.putUChar(key, value);
}

int32_t StageLink::ConfigManager::getInt(const char *key, int32_t defaultValue)
{
    return preferences.getInt(key, defaultValue);
}

void StageLink::ConfigManager::putInt(const char *key, int32_t value)
{
    preferences.putInt(key, value);
}

float StageLink::ConfigManager::getFloat(const char *key, float defaultValue)
{
    return preferences.getFloat(key, defaultValue);
}

void StageLink::ConfigManager::putFloat(const char *key, float value)
{
    preferences.putFloat(key, value);
}

bool StageLink::ConfigManager::getBool(const char *key, bool defaultValue)
{
    return preferences.getBool(key, defaultValue);
}

void StageLink::ConfigManager::putBool(const char *key, bool value)
{
    preferences.putBool(key, value);
}

void StageLink::ConfigManager::resetToDefaults()
{
    preferences.clear();
}
