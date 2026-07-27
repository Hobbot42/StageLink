// StageLink RxQ ActionEngine
// Generic execution layer between a cue's actions and OutputManager:
// SHOW -> CUE -> ActionEngine -> OutputManager. Deliberately knows
// nothing about ShowEngine, cues, or shows - it only ever sees a plain
// array of Action (see Action.h), so it's reusable for anything that
// has actions to run, not just ShowEngine's current cue.
//
// executeActions() takes a fade time. With fadeMs 0 it is a single
// immediate pass, exactly as before: every action is forwarded to
// OutputManager in order, with no timing. With a fade time it instead
// starts a ramp per channel and returns straight away - tick() then walks
// those ramps toward their targets on later loops, so a cue rises over
// time rather than snapping.
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
    // One ramp per channel a single cue can drive. Matches
    // ShowEngine::MAX_ACTIONS_PER_CUE - a cue can't produce more
    // simultaneous ramps than it has actions.
    static constexpr uint8_t MAX_RAMPS = 8;

    // Applies actions[0..actionCount) to outputManager.
    //
    // fadeMs 0 sets every value immediately. Any other value ramps each
    // action's channel from its current value to the action's value over
    // fadeMs, cancelling any ramp already running on that channel - call
    // tick() every loop for those to progress.
    void executeActions(
        const Action *actions,
        uint8_t actionCount,
        StageLink::OutputManager &outputManager,
        uint32_t fadeMs = 0
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
        uint32_t startMs;
        uint32_t durationMs;
        bool active;
    };

    // Stops any ramp on this channel, so a new one replaces it rather
    // than the two fighting over the same output.
    void cancelChannel(uint8_t channel);

    Ramp ramps_[MAX_RAMPS] = {};
};
