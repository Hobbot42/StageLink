#pragma once

#include <cstdint>
#include "EffectEngine.h"

// StageLink TriggerManager
// Generic layer between "something happened" (a trigger event) and
// "start whichever effect is assigned to it" - see EffectEngine.h for
// playback itself and EffectStorage.h for where effects are persisted.
// Same indirection relationship OutputManager has to channel numbers:
// TriggerManager doesn't know or care what a triggerID physically is
// (a wireless cue, a local button, eventually a timer...), only that
// it's a small integer that may or may not have an effect assigned.
//
// FxQ Phase 2.7: this only wires up assignment bookkeeping and the
// existing cue/local-button sources - it does not yet let EffectEngine
// hold more than one loaded effect at a time (see EffectEngine.h), so
// every assignment in this first version points at the same effect.
// getAssignedEffect() still returns a real, distinct effectID per
// trigger so a future EffectEngine that can switch between multiple
// loaded effects can use this as-is, without changing TriggerManager's
// API.
//
// Deliberately not included yet: persisting assignments via
// ConfigManager (hardcoded defaults are fine for now - see RX main.cpp),
// hardware-specific trigger sources beyond cue/local button, timers.
// Belongs to: StageLink_Common.

namespace StageLink
{
    // Generic trigger identifiers, not tied to specific hardware - a
    // caller is free to define its own IDs beyond these (TriggerManager
    // is agnostic to what a triggerID means), same relationship
    // OutputManager has to channel numbers. TRIGGER_LOCAL_BUTTON_2 has
    // no hardware behind it yet on any board - defined now so the ID
    // space is reserved, not because anything targets it yet.
    constexpr uint8_t TRIGGER_CUE_BUTTON = 0;
    constexpr uint8_t TRIGGER_LOCAL_BUTTON_1 = 1;
    constexpr uint8_t TRIGGER_LOCAL_BUTTON_2 = 2;

    class TriggerManager
    {
    public:
        static constexpr uint8_t MAX_TRIGGERS = 8;
        static constexpr uint8_t NO_EFFECT_ASSIGNED = 0xFF;

        // engine must outlive the TriggerManager - same non-owning
        // pointer-via-reference pattern as EffectEngine::begin().
        void begin(EffectEngine &engine);

        // Assigns effectID to triggerID, replacing any previous
        // assignment. Silently ignored if all MAX_TRIGGERS slots are
        // already assigned to other trigger IDs.
        void assignTrigger(uint8_t triggerID, uint8_t effectID);

        // Removes triggerID's assignment - getAssignedEffect() and
        // trigger() both then treat it as unassigned.
        void clearTrigger(uint8_t triggerID);

        // Returns the effectID assigned to triggerID, or
        // NO_EFFECT_ASSIGNED if nothing is.
        uint8_t getAssignedEffect(uint8_t triggerID) const;

        // Fires triggerID: looks up its assigned effect and, if one
        // exists, starts it via EffectEngine::startEffect(). No-op if
        // triggerID has no assignment or begin() was never called.
        //
        // Calls startEffect() with no effectID argument because
        // EffectEngine currently only ever holds one loaded effect at a
        // time (see EffectEngine.h) - this still checks the assignment
        // first (a trigger with no effect assigned does nothing, rather
        // than always playing whatever happens to be loaded), so the
        // behavior is already correct once EffectEngine gains the
        // ability to load more than one effect.
        void trigger(uint8_t triggerID);

    private:
        struct Binding
        {
            uint8_t triggerID;
            uint8_t effectID;
        };

        Binding bindings[MAX_TRIGGERS] = {};
        uint8_t count = 0;
        EffectEngine *engine = nullptr;
    };
}
