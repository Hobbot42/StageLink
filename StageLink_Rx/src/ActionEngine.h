// StageLink RxQ ActionEngine
// Generic execution layer between a cue's actions and OutputManager:
// SHOW -> CUE -> ActionEngine -> OutputManager. Deliberately knows
// nothing about ShowEngine, cues, or shows - it only ever sees a plain
// array of Action (see Action.h), so it's reusable for anything that
// has actions to run, not just ShowEngine's current cue.
//
// executeActions() returns straight away and does the work over time.
// Each action carries an optional delay and an optional fade (see
// Action.h); an action with neither is applied immediately, exactly as
// before any timing existed. Otherwise it becomes a ramp, and tick()
// walks those ramps toward their targets on later loops - so a cue can
// stagger its actions rather than moving everything at once.
//
// A ramp starts from OutputManager::lastValue() - wherever the output
// actually is - not from an assumed zero. That is what makes an
// interrupted fade behave: firing a new cue mid-fade cancels the ramps on
// the channels it drives and starts new ones from the values reached so
// far, so the outputs keep moving instead of jumping.
//
// Only ActionCommand::Level is handled; Color/State are recognized but
// intentionally no-ops (see Action.h) until their own command handling
// exists.
// Belongs to: StageLink_Rx.

#pragma once

#include <cstdint>
#include "Action.h"
#include "OutputManager.h"

class ActionEngine
{
public:
    // One ramp per channel a single cue can drive - a cue can't produce
    // more simultaneous ramps than it has actions, so this tracks
    // ShowEngine::MAX_ACTIONS_PER_CUE. Not referencing it directly keeps
    // ActionEngine free of any dependency on ShowEngine.
    static constexpr uint8_t MAX_RAMPS = 16;

    // Applies actions[0..actionCount) to outputManager.
    //
    // cueFadeMs is the cue's own fade time, used by any action that
    // hasn't been given a fade of its own (ACTION_FADE_FROM_CUE - see
    // Action.h). An action with no delay and no fade is applied
    // immediately; anything else becomes a ramp, and tick() has to be
    // called every loop for those to progress.
    void executeActions(
        const Action *actions,
        uint8_t actionCount,
        StageLink::OutputManager &outputManager,
        uint32_t cueFadeMs = 0
    );

    // Advances any running ramps. Call every loop(); a no-op when nothing
    // is fading.
    void tick(StageLink::OutputManager &outputManager);

    // True while at least one ramp is still running - lets a display show
    // that a cue is still on its way in.
    bool isFading() const;

    // Drops every ramp without touching the outputs, leaving them
    // wherever they had reached.
    void cancelAll();

private:
    struct Ramp
    {
        uint8_t channel;
        int32_t from;
        int32_t to;
        uint32_t startMs;   // when the ramp begins - later than now if delayed
        uint32_t durationMs;
        bool active;

        // Whether the delay has expired and "from" has been captured.
        // See executeActions() on why it isn't captured up front.
        bool started;
    };

    // Stops any ramp on this channel, so a new one replaces it rather
    // than the two fighting over the same output.
    void cancelChannel(uint8_t channel);

    Ramp ramps_[MAX_RAMPS] = {};
};
