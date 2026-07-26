// StageLink RxQ ShowEngine
// RxQ's show system: SHOW -> CUE -> ACTION[] -> ActionEngine ->
// OutputManager. Tracks a show's cues (number/name/actions) and the
// current/selected cue, and can execute the current cue's actions on
// demand (executeCurrentCue()) by handing them to ActionEngine (see
// ActionEngine.h) - ShowEngine itself no longer talks to OutputManager
// directly, it only finds which cue is current and delegates. Still no
// persistence, no cue editing, no fades/timing - an action is applied
// immediately and only when explicitly executed.
//
// currentCue is the cue considered "already run" (what go() last
// landed on, and what executeCurrentCue() acts on); selectedCue is what
// nextCue()/previousCue() move around and what go() will land on next.
// Right after begin()/loadTestShow(), and after every home(),
// selectedCue is currentCue + 1 - the natural "what to run next"
// position. Deliberately not bounds-checked against getCueCount() yet
// (selectedCue can exceed it, e.g. after go()-ing the last cue) - cue
// navigation is pure state tracking, not validation against real cue
// data.
//
// go()/nextCue()/previousCue()/home() never touch OutputManager
// themselves - only executeCurrentCue() does, and only when a caller
// (see StageLink_Rx/src/GuiController.cpp) explicitly calls it, e.g.
// right after go(). Belongs to: StageLink_Rx.

#pragma once

#include <cstdint>
#include "Action.h"
#include "ActionEngine.h"
#include "OutputManager.h"

class ShowEngine
{
public:
    // Loads the hardcoded test show - see loadTestShow(). A future
    // version will load a real stored show instead; kept as a separate
    // call from loadTestShow() itself so that swap doesn't change
    // begin()'s signature.
    void begin();

    // Hardcoded test show for this foundation phase: "Dragon Battle",
    // 3 named cues, each with one LEVEL action on the servo output.
    // Resets currentCue to 1 and selectedCue to 2.
    void loadTestShow();

    // Selected cue becomes the current cue, then the selection advances
    // to current + 1 (same relationship as right after begin()) - no
    // actions are triggered, this only updates state.
    void go();

    // Resets the selection back to current + 1, without changing the
    // current cue - e.g. after navigating away to look at other cues.
    void home();

    // Moves the selection forward/backward by one. previousCue() stops
    // at 1 (never selects cue 0 or below); nextCue() has no upper bound
    // here - see the class comment.
    void nextCue();
    void previousCue();

    const char *getShowName() const;
    uint8_t getCurrentCue() const;
    uint8_t getSelectedCue() const;
    uint8_t getCueCount() const;

    // Name of cueNumber (1-based, matching getCurrentCue()/
    // getSelectedCue()), or an empty string if cueNumber isn't part of
    // the loaded show.
    const char *getCueName(uint8_t cueNumber) const;

    // Finds the cue matching getCurrentCue() and hands its actions to
    // ActionEngine::executeActions() (see ActionEngine.h) - no-op if the
    // current cue has no actions or isn't found. Does not itself get
    // called by go()/nextCue()/previousCue()/home() - a caller decides
    // when to execute, typically right after go() (see
    // GuiController.cpp). Signature unchanged from before ActionEngine
    // existed, so existing callers don't need to change.
    void executeCurrentCue(StageLink::OutputManager &outputManager);

private:
    static constexpr uint8_t SHOW_NAME_SIZE = 32;
    static constexpr uint8_t CUE_NAME_SIZE = 24;
    static constexpr uint8_t MAX_CUES = 8; // headroom beyond the 3-cue test show; fixed-size, no dynamic memory
    static constexpr uint8_t MAX_ACTIONS_PER_CUE = 4; // headroom beyond the 1-action-per-cue test show

    struct Cue
    {
        uint8_t number;
        char name[CUE_NAME_SIZE];
        Action actions[MAX_ACTIONS_PER_CUE];
        uint8_t actionCount;
    };

    void setCue(uint8_t index, uint8_t number, const char *name);
    void addAction(uint8_t cueIndex, uint8_t outputId, ActionCommand command, int32_t value);

    char showName_[SHOW_NAME_SIZE] = {};
    Cue cues_[MAX_CUES] = {};
    uint8_t cueCount_ = 0;
    uint8_t currentCue_ = 1;
    uint8_t selectedCue_ = 1;
    ActionEngine actionEngine_;
};
