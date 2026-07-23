#include "EffectStorage.h"

#include <cstdio>
#include "ConfigManager.h"

namespace
{
    constexpr size_t KEY_BUFFER_SIZE = 16;

    // NVS (via ESP32 Preferences, see ConfigManager.cpp) caps keys at 15
    // characters - these patterns (effectID 0 to MAX_EFFECTS - 1, step
    // index 0 to MAX_EFFECT_STEPS - 1) stay well under that even at
    // their longest ("fx3s15dly" = 9 chars).
    void buildEnabledKey(uint8_t effectID, char *buffer, size_t bufferSize)
    {
        snprintf(buffer, bufferSize, "fx%uen", static_cast<unsigned>(effectID));
    }

    void buildCountKey(uint8_t effectID, char *buffer, size_t bufferSize)
    {
        snprintf(buffer, bufferSize, "fx%ucnt", static_cast<unsigned>(effectID));
    }

    void buildStepChannelKey(uint8_t effectID, uint8_t stepIndex, char *buffer, size_t bufferSize)
    {
        snprintf(buffer, bufferSize, "fx%us%uch", static_cast<unsigned>(effectID), static_cast<unsigned>(stepIndex));
    }

    void buildStepValueKey(uint8_t effectID, uint8_t stepIndex, char *buffer, size_t bufferSize)
    {
        snprintf(buffer, bufferSize, "fx%us%uval", static_cast<unsigned>(effectID), static_cast<unsigned>(stepIndex));
    }

    void buildStepDelayKey(uint8_t effectID, uint8_t stepIndex, char *buffer, size_t bufferSize)
    {
        snprintf(buffer, bufferSize, "fx%us%udly", static_cast<unsigned>(effectID), static_cast<unsigned>(stepIndex));
    }
}

void StageLink::EffectStorage::begin()
{
    // Nothing to do yet - see the header comment.
}

void StageLink::EffectStorage::saveEffect(uint8_t effectID, const Effect &effect)
{
    if (effectID >= MAX_EFFECTS)
    {
        return;
    }

    char key[KEY_BUFFER_SIZE];

    buildEnabledKey(effectID, key, sizeof(key));
    ConfigManager::putBool(key, true);

    buildCountKey(effectID, key, sizeof(key));
    ConfigManager::putUInt8(key, effect.stepCount);

    for (uint8_t i = 0; i < effect.stepCount; ++i)
    {
        const EffectStep &step = effect.steps[i];

        buildStepChannelKey(effectID, i, key, sizeof(key));
        ConfigManager::putUInt8(key, step.channel);

        buildStepValueKey(effectID, i, key, sizeof(key));
        ConfigManager::putInt(key, step.value);

        buildStepDelayKey(effectID, i, key, sizeof(key));
        ConfigManager::putInt(key, static_cast<int32_t>(step.delayAfterMs));
    }
}

bool StageLink::EffectStorage::loadEffect(uint8_t effectID, Effect &outEffect)
{
    if (!hasEffect(effectID))
    {
        return false;
    }

    char key[KEY_BUFFER_SIZE];

    buildCountKey(effectID, key, sizeof(key));
    uint8_t stepCount = ConfigManager::getUInt8(key, 0);

    if (stepCount > MAX_EFFECT_STEPS)
    {
        stepCount = MAX_EFFECT_STEPS;
    }

    clearEffect(outEffect);

    for (uint8_t i = 0; i < stepCount; ++i)
    {
        buildStepChannelKey(effectID, i, key, sizeof(key));
        uint8_t channel = ConfigManager::getUInt8(key, 0);

        buildStepValueKey(effectID, i, key, sizeof(key));
        int32_t value = ConfigManager::getInt(key, 0);

        buildStepDelayKey(effectID, i, key, sizeof(key));
        uint32_t delayAfterMs = static_cast<uint32_t>(ConfigManager::getInt(key, 0));

        addEffectStep(outEffect, channel, value, delayAfterMs);
    }

    return true;
}

void StageLink::EffectStorage::deleteEffect(uint8_t effectID)
{
    if (effectID >= MAX_EFFECTS)
    {
        return;
    }

    char key[KEY_BUFFER_SIZE];
    buildEnabledKey(effectID, key, sizeof(key));
    ConfigManager::putBool(key, false);
}

bool StageLink::EffectStorage::hasEffect(uint8_t effectID)
{
    if (effectID >= MAX_EFFECTS)
    {
        return false;
    }

    char key[KEY_BUFFER_SIZE];
    buildEnabledKey(effectID, key, sizeof(key));
    return ConfigManager::getBool(key, false);
}
