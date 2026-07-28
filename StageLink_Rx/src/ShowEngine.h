// StageLink RxQ ShowEngine
// RxQ's show system and the single place shows live: SHOW[] -> CUE ->
// ACTION[] -> ActionEngine -> OutputManager. Holds every show on the
// device, tracks which one is selected, tracks the current/selected cue
// within it, and executes a cue's actions on demand
// (executeCurrentCue()) by handing them to ActionEngine (see
// ActionEngine.h) - ShowEngine itself never talks to OutputManager
// directly, it only finds which actions to run and delegates.
//
// Both the GUI's Show Mode and its Program Mode read and write through
// here (see GuiController.h) - there is no second copy of show data
// anywhere. The whole show list is held in RAM and written to the
// filesystem shortly after editing stops (see tick()/ShowStorage.h), so
// programming survives a power cycle.
//
// currentCue is the cue considered "already run" (what go() last landed
// on, and what executeCurrentCue() acts on); selectedCue is what
// nextCue()/previousCue() move around and what go() will land on next.
// Right after begin()/loadTestShow(), and after every home(),
// selectedCue is currentCue + 1 - the natural "what to run next"
// position, clamped so it never points past the last programmed cue.
// Navigation stops at both ends of the show rather than running into
// cue numbers that don't exist.
//
// go()/nextCue()/previousCue()/home() never touch OutputManager
// themselves - only executeCurrentCue() and executeCue() do, and only
// when a caller (see StageLink_Rx/src/GuiController.cpp) explicitly
// calls one. Belongs to: StageLink_Rx.

#pragma once

#include <cstdint>
#include "Action.h"
#include "ActionEngine.h"
#include "OutputManager.h"

class ShowEngine
{
public:
    // Storage is no longer the limit - that is a 1.4MB filesystem (see
    // ShowStorage.h). Static RAM is: every show is resident, and the
    // ESP32's .bss segment is far smaller than its total RAM, so these
    // numbers are what fits rather than what the filesystem could hold.
    // Holding only the selected show in memory and paging the rest from
    // disk would lift this considerably and needs no storage change.
    static constexpr uint8_t MAX_SHOWS = 8;
    static constexpr uint8_t MAX_CUES = 32;

    // A cue that sets a servo position plus a full LED color needs 5
    // actions on its own (servo + R/G/B/brightness), so this has to be
    // comfortably above the one-action-per-cue the first test show used.
    static constexpr uint8_t MAX_ACTIONS_PER_CUE = 16;

    static constexpr uint8_t SHOW_NAME_SIZE = 32;
    static constexpr uint8_t CUE_NAME_SIZE = 24;

    // 0 to 99.9 seconds, held in tenths - see Cue::fadeTenths.
    static constexpr uint16_t MAX_FADE_TENTHS = 999;

    // Loads the saved show list from flash, or comes up empty if nothing
    // is stored (a fresh unit, or firmware whose show layout has changed
    // - see ShowStorage.h). A unit ships with no shows in firmware; the
    // operator creates them in Program Mode.
    void begin();

    // Call every loop(). Advances any cue fade in progress, and writes
    // edits to flash once they have been quiet for AUTOSAVE_QUIET_MS, so
    // a burst of encoder clicks costs one write instead of one per click.
    void tick(StageLink::OutputManager &outputManager);

    // True while a cue is still fading in - see ActionEngine::isFading().
    bool isFading() const;

    // Writes the show list to flash now, regardless of the autosave
    // timer. Returns false if the write failed.
    bool save();

    // True when there are edits that have not reached flash yet - the
    // display can use this to show a "saving" indicator.
    bool hasUnsavedChanges() const;

    // TEST DATA ONLY - not something a shipped unit ever calls. Replaces
    // the whole show list with one demo show ("Dragon Battle", 3 cues
    // that each drive both real outputs) so there's something to run and
    // edit before show creation and persistence are finished. main.cpp
    // calls this behind LOAD_TEST_SHOW_ON_BOOT; deleting that call is all
    // that's needed to get shipping behavior.
    void loadTestShow();

    // --- Show selection -------------------------------------------------
    // Which show every cue/action call below refers to. Selecting a show
    // resets cue position to the same "start of show" state loadTestShow()
    // leaves behind, so switching shows never leaves a cue pointer from
    // the previous show behind.
    uint8_t getShowCount() const;
    uint8_t getSelectedShowIndex() const;
    void selectShow(uint8_t showIndex);

    // Name of the selected show, or of showIndex specifically. The
    // no-argument form is what Show Mode's display uses.
    const char *getShowName() const;
    const char *getShowName(uint8_t showIndex) const;

    // Appends an empty show carrying the generated default name
    // ("Show 01"), flagged autoName so it keeps following its position
    // until the operator renames it. Returns false (and changes nothing)
    // if MAX_SHOWS is already reached. Does not select it.
    bool addShow();

    // Gives a show a name of the operator's choosing, which stops it
    // tracking its position - see the Show struct.
    bool renameShow(uint8_t showIndex, const char *name);

    // Duplicates a show, contents and all, onto the end of the list. The
    // copy gets fresh internal ids throughout, so nothing references the
    // original. Returns false if MAX_SHOWS is reached.
    bool copyShow(uint8_t showIndex);

    // Removes a show and everything in it.
    bool removeShow(uint8_t showIndex);

    // --- Cue navigation -------------------------------------------------

    // Selected cue becomes the current cue, then the selection advances
    // to current + 1 (same relationship as right after begin()) - no
    // actions are triggered, this only updates state.
    void go();

    // Resets the selection back to current + 1, without changing the
    // current cue - e.g. after navigating away to look at other cues.
    void home();

    // Moves the selection forward/backward by one, stopping at the ends
    // of the programmed cues: previousCue() never goes below 1, and
    // nextCue() never goes past the last cue in the show.
    void nextCue();
    void previousCue();

    uint8_t getCurrentCue() const;
    uint8_t getSelectedCue() const;

    // Number of cues in the selected show.
    uint8_t getCueCount() const;

    // Name of cueNumber (1-based, matching getCurrentCue()/
    // getSelectedCue()), or an empty string if cueNumber isn't part of
    // the selected show.
    const char *getCueName(uint8_t cueNumber) const;

    // --- Cue editing ----------------------------------------------------
    // These address cues by 0-based *index* (position in the list), not
    // by the 1-based cue number the navigation calls above use - the
    // editor walks a list, so position is what it has to hand.

    const char *getCueNameAt(uint8_t cueIndex) const;

    // How long this cue takes to reach its values when GOne, in tenths of
    // a second. 0 is an instant snap.
    uint16_t getCueFadeTenths(uint8_t cueIndex) const;
    bool setCueFadeTenths(uint8_t cueIndex, uint16_t tenths);

    // Per-action timing. Both apply to the whole range an edited action
    // covers, since one edited action can be several stored actions (an
    // LED colour is four channels) and they have to move together.
    // Reading returns the first action's value, which is what the editor
    // wrote across the range.
    uint16_t getActionDelayTenths(uint8_t cueIndex, uint8_t actionIndex) const;
    uint16_t getActionFadeTenths(uint8_t cueIndex, uint8_t actionIndex) const;
    bool setActionTiming(
        uint8_t cueIndex,
        uint8_t startIndex,
        uint8_t count,
        uint16_t delayTenths,
        uint16_t fadeTenths
    );

    // Appends an empty cue carrying the generated default name
    // ("Cue 01"), flagged autoName. Returns false (and changes nothing)
    // if MAX_CUES is reached.
    bool addCue();

    // Gives a cue a name of the operator's choosing, which stops it
    // tracking its position - see the Cue struct.
    bool renameCue(uint8_t cueIndex, const char *name);

    // Duplicates a cue and its actions directly below the original, so
    // the copy runs next. The copy gets a fresh internal id. Returns
    // false if MAX_CUES is reached.
    bool copyCue(uint8_t cueIndex);

    // Removes a cue and everything in it. Remaining cues are renumbered
    // so numbering stays 1..getCueCount() with no gaps - GO addresses
    // cues by number, and a hole would leave a selectable cue that
    // displays as blank.
    bool removeCue(uint8_t cueIndex);

    // Swaps the cue at firstIndex with the one after it, then renumbers
    // both. Returns false if there is no cue after firstIndex.
    bool swapAdjacentCues(uint8_t firstIndex);

    // --- Action editing -------------------------------------------------
    // Actions are stored flat, exactly as ActionEngine consumes them: one
    // Action per output channel per cue. A single edit in the GUI can map
    // to several of these (an LED color is four - see GuiController.h),
    // which is why the write calls take a range rather than one Action.

    uint8_t getActionCount(uint8_t cueIndex) const;

    // Pointer to the cue's action array, or nullptr if cueIndex is out of
    // range. Valid until the next call that edits this cue.
    const Action *getActions(uint8_t cueIndex) const;

    // Replaces removeCount actions starting at startIndex with the count
    // actions in actions[]. Passing removeCount 0 inserts; passing a
    // null/zero actions list deletes. Returns false and changes nothing
    // if the result wouldn't fit or the range is invalid.
    bool replaceActions(
        uint8_t cueIndex,
        uint8_t startIndex,
        uint8_t removeCount,
        const Action *actions,
        uint8_t count
    );

    // Swaps two adjacent blocks of actions: the firstLength actions at
    // firstStart, and the secondLength actions immediately after them.
    // Blocks rather than single actions because one edited action can be
    // several stored actions - see GuiController.h. Returns false and
    // changes nothing if the two blocks don't fit inside the cue.
    bool swapAdjacentActions(
        uint8_t cueIndex, uint8_t firstStart, uint8_t firstLength, uint8_t secondLength
    );

    // --- Execution ------------------------------------------------------

    // Finds the cue matching getCurrentCue() in the selected show and
    // hands its actions to ActionEngine::executeActions() (see
    // ActionEngine.h) - no-op if that cue has no actions or isn't found.
    // Not called by go()/nextCue()/previousCue()/home() - a caller
    // decides when to execute, typically right after go().
    void executeCurrentCue(StageLink::OutputManager &outputManager);

    // Same, for a cue addressed by index - used to audition a cue while
    // editing it, without disturbing the current/selected cue state.
    void executeCue(uint8_t cueIndex, StageLink::OutputManager &outputManager);

private:
    // Identity is the id; it is allocated once at creation and never
    // changes, so moving or renumbering can't make a stored reference
    // point at a different cue. The execution number (Q01, Q02...) is
    // deliberately NOT stored - it is the cue's position in the list, so
    // reordering and deleting can't leave numbering out of step with what
    // actually runs.
    //
    // autoName tracks whether the name is still the generated default
    // ("Cue 01"). While it is, the name follows the position; once the
    // operator renames it, the name is theirs and moving never rewrites
    // it. See refreshCueAutoNames().
    struct Cue
    {
        uint16_t id;
        char name[CUE_NAME_SIZE];
        bool autoName;

        // Fade time in tenths of a second (0-999, so 0 to 99.9s). Tenths
        // rather than milliseconds because that is the resolution the
        // operator actually sets, and it keeps the stored cue small.
        // 0 means snap, which is how every cue behaved before fading.
        uint16_t fadeTenths;
        Action actions[MAX_ACTIONS_PER_CUE];
        uint8_t actionCount;
    };

    // Same identity rules as Cue - see above. A show's displayed number
    // is its position in the show list and isn't stored either.
    struct Show
    {
        uint16_t id;
        char name[SHOW_NAME_SIZE];
        bool autoName;
        Cue cues[MAX_CUES];
        uint8_t cueCount;
    };

    // Rewrites the generated names of any cues/shows still on autoName,
    // so defaults keep matching their position after an add, move,
    // delete or copy. Renamed items are left alone.
    // Records that the show list changed, restarting the autosave timer.
    // Called by every operation that edits shows, cues or actions - an
    // edit that forgets this stays in RAM and is lost on reset.
    void markDirty();

    void refreshCueAutoNames();
    void refreshShowAutoNames();

    // Highest cue number in the selected show, or 0 if it has no cues.
    // Numbering is positional, so this is just the cue count.
    uint8_t lastCueNumber() const;

    // cueNumber held inside the programmed range - see nextCue()/go().
    uint8_t clampCueNumber(uint8_t cueNumber) const;

    Show *selectedShow();
    const Show *selectedShow() const;
    Cue *cueAt(uint8_t cueIndex);
    const Cue *cueAt(uint8_t cueIndex) const;

    void setCue(uint8_t showIndex, uint8_t index, uint8_t number, const char *name);
    void addTestAction(uint8_t showIndex, uint8_t cueIndex, uint8_t outputId, ActionCommand command, int32_t value);

    Show shows_[MAX_SHOWS] = {};
    uint8_t showCount_ = 0;

    // Monotonic - never reused, so an id freed by a delete can't come
    // back attached to a different object.
    uint16_t nextShowId_ = 1;
    uint16_t nextCueId_ = 1;

    // How long the edits have to stop before a save runs. Long enough
    // that turning the encoder through a value is one write, short enough
    // that pulling the power shortly after an edit still keeps it.
    static constexpr uint32_t AUTOSAVE_QUIET_MS = 1500;

    bool dirty_ = false;
    uint32_t lastChangeMs_ = 0;
    uint8_t selectedShowIndex_ = 0;

    uint8_t currentCue_ = 1;
    uint8_t selectedCue_ = 1;
    ActionEngine actionEngine_;
};
