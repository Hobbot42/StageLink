#pragma once

#include <cstdint>
#include "OutputManager.h"

// StageLink EffectEngine
// Foundation for RxQ-local effect playback: a fixed sequence of output
// channel/value steps, each followed by a wait, played back without
// blocking (see EffectEngine::update()). This is FxQ Phase 2.5 -
// RxQ stops being a pure remote-controlled receiver and gains the
// ability to store and run a simple effect on its own; TxQ is meant to
// eventually become a trigger/programmer for effects RxQ runs locally.
// This first version only proves the playback mechanism itself: one
// effect, loaded in memory, started/stopped locally. Deliberately not
// included yet - saved/multiple effects, local physical triggers,
// timers, a wireless RUN_EFFECT command, or effect editing from TxQ.
//
// EffectEngine only ever talks to OutputManager, never to a specific
// OutputDevice or hardware directly - the same channel-routing
// indirection VALUE_UPDATE/STATE_SNAPSHOT already go through, so an
// effect step and a live radio command apply to hardware exactly the
// same way, indistinguishable to the OutputDevice on the receiving end.
// Belongs to: StageLink_Common (playback logic isn't RX-specific, even
// though only RX runs it today).

namespace StageLink
{
    constexpr uint8_t MAX_EFFECT_STEPS = 16;

    struct EffectStep
    {
        uint8_t channel;       // OutputManager channel - same numbering VALUE_UPDATE routing uses
        int32_t value;         // applied via OutputManager::update(channel, value)
        uint32_t delayAfterMs; // how long to wait after this step before starting the next
    };

    struct Effect
    {
        EffectStep steps[MAX_EFFECT_STEPS];
        uint8_t stepCount;
    };

    inline void clearEffect(Effect &effect)
    {
        effect.stepCount = 0;
    }

    // Appends one step. Same append-only pattern as
    // StateSnapshot.h's addOrUpdateStateItem / OutputList.h's
    // addOutputDescriptor - callers build an Effect without tracking
    // indices themselves.
    inline bool addEffectStep(
        Effect &effect,
        uint8_t channel,
        int32_t value,
        uint32_t delayAfterMs
    )
    {
        if (effect.stepCount >= MAX_EFFECT_STEPS)
        {
            return false;
        }

        EffectStep &step = effect.steps[effect.stepCount];
        step.channel = channel;
        step.value = value;
        step.delayAfterMs = delayAfterMs;
        effect.stepCount++;

        return true;
    }

    // Non-blocking local playback of one loaded Effect at a time. Never
    // touches hardware directly - every step's value goes through
    // OutputManager::update(), same as any other channel update.
    class EffectEngine
    {
    public:
        // manager must outlive the EffectEngine - same non-owning
        // reference-via-pointer pattern as LEDChannelProxy's owner.
        void begin(OutputManager &manager);

        // Copies effect into internal storage, replacing whatever was
        // loaded before. Does not start playback - call startEffect()
        // separately.
        void loadEffect(const Effect &effect);

        // Starts playback from step 0 of the loaded effect. No-op if
        // nothing is loaded, the loaded effect has no steps, or begin()
        // was never called. Safe to call again while already running -
        // restarts from step 0.
        void startEffect();

        // Stops playback immediately, mid-step if necessary. Whatever
        // value the last-executed step applied stays applied - stopping
        // doesn't revert or re-home outputs.
        void stopEffect();

        bool isRunning() const;

        // Call every loop(). Rate-limited internally via millis() (no
        // delay()) - a no-op most calls, only actually advances once the
        // current step's delayAfterMs has elapsed.
        void update();

    private:
        void executeCurrentStep();

        OutputManager *outputManager = nullptr;
        Effect loadedEffect = {};
        bool hasLoadedEffect = false;
        bool running = false;
        uint8_t currentStepIndex = 0;
        unsigned long stepStartTime = 0;
    };
}
