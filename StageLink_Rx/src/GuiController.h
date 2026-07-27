// FxQ GUI Architecture Prototype v0.1
// Navigation-only prototype for FxQ's eventual production interface:
// SHOW MODE (view the running show - see below), PROGRAM MODE (Show ->
// Cue -> Action -> Output -> Command -> Value -> Cue Time), and SETUP
// MODE (device configuration). PROGRAM MODE's show/cue/action data is
// still fake and in-memory (there is no persistence, and editing it does
// not yet change what Show Mode plays back) - but its Value Entry screen
// now drives real hardware live, see below.
//
// SHOW MODE (Screen::ShowRun) is the one exception: it reads and drives
// the real ShowEngine (see ShowEngine.h) rather than PROGRAM MODE's fake
// data - rotate moves ShowEngine's selected cue (nextCue()/
// previousCue()), a press GOes (go()) and then executes that cue's
// actions against the real OutputManager (executeCurrentCue() - see
// handlePress()'s Screen::ShowRun case).
//
// PROGRAM MODE's Value Entry is the other place real output happens: the
// output catalog maps to real OutputManager channels, and every encoder
// click applies the whole action being edited to the hardware
// immediately (see previewCurrentEdit()), so a color or servo position
// can be dialled in by eye. Where that action gets *stored* is still
// fake - Program Mode writes to shows_[] here, not to ShowEngine, so a
// GO in Show Mode won't replay it yet.
//
// Input: rotate navigates or edits a value, a short press selects/
// confirms/GOes, a hold goes back/cancels - same "hold = alternate
// action" convention as StageLink_Common/src/LabelEditor.h. Every
// screen behaves the same way for consistency.
//
// The existing diagnostic pages (Status/Diagnostics/Effect Test/Trigger
// Status) are not touched or removed - Setup Mode's "Diagnostics" entry
// hands control back to RX main.cpp's existing pageCycler system
// unchanged (see the legacyModeCallback passed to begin()).
// Belongs to: StageLink_Rx - the fake data and flow here are specific to
// this prototype, not something TX or another RX-class device reuses.

#pragma once

#include <cstdint>
#include "ActionEngine.h"
#include "DeviceInfo.h"
#include "LabelEditor.h"
#include "ReliableRadio.h"
#include "OutputManager.h"
#include "ShowEngine.h"

class GuiController
{
public:
    // deviceInfo/labelEditor are RX main.cpp's existing objects, reused
    // as-is for the Setup > Unit Label flow rather than duplicating
    // label-editing logic here. commitUnitLabel is main.cpp's existing
    // commitUnitLabelEdit() (persists the edited label); enterLegacyMode
    // is called when the operator selects Setup > Diagnostics. radio is
    // read (never sent through) for Show Mode's signal indicator only.
    // showEngine is RX main.cpp's ShowEngine instance - Show Mode reads
    // its cue state to render and drives its navigation (go()/nextCue()/
    // previousCue() - see handlePress()/handleRotate()'s Screen::ShowRun
    // cases). outputManager is passed straight through to
    // showEngine.executeCurrentCue() right after go() - GuiController
    // itself never calls OutputManager::update() directly. enterUpdateMode
    // is called when the operator selects Setup > Update Mode (see
    // UpdateMode.h) - same shape as enterLegacyMode.
    void begin(
        StageLink::DeviceInfo &deviceInfo,
        StageLink::LabelEditor &labelEditor,
        StageLink::ReliableRadio &radio,
        StageLink::OutputManager &outputManager,
        ShowEngine &showEngine,
        void (*commitUnitLabel)(),
        void (*enterLegacyMode)(),
        void (*enterUpdateMode)()
    );

    // Encoder rotation: navigate a list, or change the field/character
    // currently being edited.
    void handleRotate(int direction);

    // Select/confirm/GO, depending on the screen. Fired by the encoder's
    // own short press and by the dedicated Action button (GPIO4 - see
    // main.cpp) - both mirror this same gesture rather than having
    // distinct behavior of their own for now.
    void handlePress();

    // Back/cancel, depending on the screen - from a root screen (Show
    // Run/Show List/Setup List) this goes up to the Mode Menu; from the
    // Mode Menu itself it does nothing (top of the hierarchy). Fired
    // only by the dedicated Back button (GPIO27 - see main.cpp) - the
    // encoder's own hold gesture no longer triggers this, now that a
    // dedicated Back control exists.
    void handleHold();

    // Renders whatever screen is currently active. Call after any
    // handle*() that changed something, and on the normal periodic
    // display refresh.
    void render();

private:
    enum class Screen : uint8_t
    {
        ShowRun,
        ModeMenu,
        ShowList,
        CueList,
        ActionList,
        OutputSelect,
        CommandSelect,
        ValueEntry,
        CueTimeEntry,
        SetupList,
        UnitLabelEdit
    };

    enum class OutputType : uint8_t
    {
        Led,
        Servo,
        Stepper,
        Relay
    };

    struct Action
    {
        uint8_t outputIndex;
        uint8_t commandIndex;
        int values[5];
        uint8_t valueCount;
    };

    static constexpr uint8_t MAX_SHOWS = 4;
    static constexpr uint8_t MAX_CUES = 6;
    static constexpr uint8_t MAX_ACTIONS = 4;
    static constexpr uint8_t MAX_STACK_DEPTH = 8;
    static constexpr uint8_t MAX_VALUE_FIELDS = 5; // sizes editValues_ - the widest fieldsFor() list
    static constexpr uint8_t LABEL_SIZE = 16; // fake-data labels only, unrelated to StageLink::Label

    struct Cue
    {
        char label[LABEL_SIZE];
        Action actions[MAX_ACTIONS];
        uint8_t actionCount;
        float timeSeconds;
    };

    struct Show
    {
        char label[LABEL_SIZE];
        Cue cues[MAX_CUES];
        uint8_t cueCount;
    };

    struct StackFrame
    {
        Screen screen;
        uint8_t selection;
    };

    void pushScreen(Screen screen);
    void popScreen();
    Screen currentScreen() const;
    uint8_t &currentSelection();

    void seedFakeData();
    OutputType outputTypeOf(uint8_t outputIndex) const;
    const char *const *commandsFor(OutputType type, uint8_t &count) const;
    const char *const *fieldsFor(OutputType type, uint8_t &count) const;

    // OutputManager channels this output type's value fields drive, in
    // the same order fieldsFor() returns them. nullptr for output types
    // with no hardware behind them yet (Stepper/Relay).
    const uint8_t *channelsFor(OutputType type) const;

    // Highest value Value Entry lets the encoder reach for this output
    // type - 180 for a servo's degrees, 255 for everything else.
    int valueMaxFor(OutputType type) const;

    // Applies the action currently being edited to real hardware right
    // now, so Value Entry previews live instead of only taking effect
    // once the action is committed and its cue is GOne. Sends every value
    // field of the action (not just the one being edited) through
    // ActionEngine - see the definition. Safe to call on any screen; a
    // no-op if the selected output has no channels behind it.
    void previewCurrentEdit();

    void beginActionFlow(bool editingExisting, uint8_t actionIndex);
    void commitActionFlow();

    StackFrame stack_[MAX_STACK_DEPTH];
    uint8_t stackDepth_ = 0;

    Show shows_[MAX_SHOWS];
    uint8_t showCount_ = 0;

    uint8_t currentShowIndex_ = 0;
    uint8_t currentCueIndex_ = 0;
    uint8_t currentActionIndex_ = 0;
    bool editingExistingAction_ = false;
    uint8_t actionFlowReturnDepth_ = 0;

    uint8_t selectedOutputIndex_ = 0;
    uint8_t selectedCommandIndex_ = 0;
    int editValues_[MAX_VALUE_FIELDS] = {};
    uint8_t editValueCount_ = 0;
    uint8_t editFieldIndex_ = 0;
    float editCueTime_ = 1.0f;

    // Scratch pointer buffer for whichever list render() is currently
    // drawing - sized to the largest list this prototype ever shows
    // (MAX_CUES + 1, the "+ Add Cue" list). Rebuilt fresh each render()
    // call, never read across calls.
    const char *itemPointers_[MAX_CUES + 2];

    StageLink::DeviceInfo *deviceInfo_ = nullptr;
    StageLink::LabelEditor *labelEditor_ = nullptr;
    StageLink::ReliableRadio *radio_ = nullptr;
    StageLink::OutputManager *outputManager_ = nullptr;
    ShowEngine *showEngine_ = nullptr;

    // Used only by previewCurrentEdit() - ShowEngine has its own instance
    // for cue playback, and reaching into it would couple the editor to
    // playback state for no benefit. ActionEngine is stateless, so a
    // second one costs nothing.
    ActionEngine previewEngine_;
    void (*commitUnitLabel_)() = nullptr;
    void (*enterLegacyMode_)() = nullptr;
    void (*enterUpdateMode_)() = nullptr;
};
