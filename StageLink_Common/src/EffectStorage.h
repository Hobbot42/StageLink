#pragma once

#include <cstdint>
#include "EffectEngine.h"

// StageLink EffectStorage
// Persists Effect data (see EffectEngine.h) across power cycles using
// ConfigManager - the only storage layer, same as everything else in
// StageLink (see ConfigManager.h). EffectStorage doesn't add a new
// storage mechanism; it just translates ConfigManager's scalar get/put
// calls into Effect-shaped save/load, the same relationship
// ConfigManager itself has to Preferences.
//
// FxQ Phase 2.6: EffectStorage owns saved effect data; EffectEngine only
// ever executes an effect once it's already in memory (RX main.cpp
// loads one via EffectStorage::loadEffect(), then hands it to
// EffectEngine::loadEffect()) - EffectStorage and EffectEngine never
// call each other directly.
//
// Each effect's fields are stored as individual ConfigManager keys (an
// exists/enabled flag, a step count, then channel/value/delayAfterMs
// per step) rather than one blob - ConfigManager has no array/blob
// storage to build on, and adding one would be a ConfigManager
// architecture change, which this phase explicitly avoids.
// Belongs to: StageLink_Common.

namespace StageLink
{
    constexpr uint8_t MAX_EFFECTS = 4;

    class EffectStorage
    {
    public:
        // No separate init work today - ConfigManager::begin() must
        // already have been called by the time any other EffectStorage
        // function runs. Exists for API symmetry with EffectEngine/
        // OutputManager, and so future storage setup has somewhere to go
        // without changing every call site.
        static void begin();

        // Persists effect under effectID (0 to MAX_EFFECTS - 1),
        // overwriting anything already stored there. Out-of-range
        // effectID is silently ignored.
        static void saveEffect(uint8_t effectID, const Effect &effect);

        // Restores the effect stored under effectID into outEffect.
        // Returns false (outEffect left untouched) if effectID is out of
        // range or nothing has been saved there yet - see hasEffect().
        static bool loadEffect(uint8_t effectID, Effect &outEffect);

        // Marks effectID as empty again - hasEffect() reports false for
        // it afterward. Stored field values aren't actively erased from
        // NVS, only the flag hasEffect() checks; a later saveEffect()
        // overwrites them anyway.
        static void deleteEffect(uint8_t effectID);

        // Whether anything has been saved under effectID.
        static bool hasEffect(uint8_t effectID);
    };
}
