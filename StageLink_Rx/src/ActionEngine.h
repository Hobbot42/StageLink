// StageLink RxQ ActionEngine
// Generic execution layer between a cue's actions and OutputManager:
// SHOW -> CUE -> ActionEngine -> OutputManager. Deliberately knows
// nothing about ShowEngine, cues, or shows - it only ever sees a plain
// array of Action (see Action.h), so it's reusable for anything that
// has actions to run, not just ShowEngine's current cue (see
// ShowEngine::executeCurrentCue(), its only caller today).
//
// executeActions() is a single, immediate pass: every action in the
// array is forwarded to OutputManager in order, with no timing, no
// delays, and no fades - a fade/wait/timed sequence is a later layer
// built on top of this, not something ActionEngine does itself. Only
// ActionCommand::Level is handled; Color/State are recognized but
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
    // Loops through every action in actions[0..actionCount) and, for
    // each one, forwards it to outputManager.update(action.outputId,
    // action.value) - only when action.command is ActionCommand::Level.
    // Any other command is skipped (recognized, not yet implemented).
    void executeActions(
        const Action *actions,
        uint8_t actionCount,
        StageLink::OutputManager &outputManager
    );
};
