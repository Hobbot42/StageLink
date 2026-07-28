// FxQ GUI Architecture Prototype v0.1
// FxQ's operator interface: SHOW MODE (pick a show, then run it),
// PROGRAM MODE (Show -> Cue -> Action -> Output -> Command -> Value) and
// SETUP MODE (device configuration).
//
// Both modes read and write the real ShowEngine (see ShowEngine.h) -
// there is no fake or duplicate show data here any more. Editing an
// action in Program Mode changes the same cue Show Mode's GO plays back.
// ShowEngine saves to flash shortly after editing stops, so programming
// survives a power cycle (see ShowEngine::tick()).
//
// Show Mode enters on a show picker (Screen::ShowSelect) rather than
// straight into the running show, so the operator chooses what they're
// about to run; selecting one calls ShowEngine::selectShow() and drops
// into Screen::ShowRun. Rotate there moves the selected cue
// (nextCue()/previousCue()), a press GOes (go()) and executes that cue's
// actions against the real OutputManager (executeCurrentCue()).
//
// Program Mode's Value Entry additionally applies the action being
// edited to the outputs on every encoder click (see
// previewCurrentEdit()), so a color or servo position can be dialled in
// by eye instead of only being seen once the cue is GOne.
//
// One edited action here can be several ShowEngine actions: the engine
// stores one Action per output *channel* (see Action.h), so an LED color
// is four of them (R/G/B/Brightness) while a servo position is one. The
// Action List groups consecutive actions that belong to the same output
// back into one row - see uiActionInfo(). ActionEngine stays unaware of
// any of this and keeps consuming a flat array.
//
// Input: rotate navigates or edits a value, a short press selects/
// confirms/GOes, and the dedicated Back button goes back or cancels one
// step. Every screen behaves the same way for consistency.
//
// The existing diagnostic pages (Status/Diagnostics/Effect Test/Trigger
// Status) are not touched or removed - Setup Mode's "Diagnostics" entry
// hands control back to RX main.cpp's existing pageCycler system
// unchanged (see the legacyModeCallback passed to begin()).
// Belongs to: StageLink_Rx.

#pragma once

#include <cstdint>
#include "Action.h"
#include "ActionEngine.h"
#include "DeviceInfo.h"
#include "LabelEditor.h"
#include "ReliableRadio.h"
#include "OutputCatalog.h"
#include "OutputManager.h"
#include "ShowEngine.h"

class GuiController
{
public:
    // deviceInfo/labelEditor are RX main.cpp's existing objects, reused
    // as-is for the Setup > Controller Label flow rather than duplicating
    // label-editing logic here. commitUnitLabel is main.cpp's existing
    // commitUnitLabelEdit() (persists the edited label); enterLegacyMode
    // is called when the operator selects Setup > Diagnostics. radio is
    // read (never sent through) for Show Mode's signal indicator only.
    // showEngine is RX main.cpp's ShowEngine instance and the only place
    // show data lives - both Show Mode and Program Mode go through it.
    // outputManager is passed to showEngine's execute calls and used for
    // Value Entry's live preview; GuiController never calls
    // OutputManager::update() directly. enterUpdateMode is called when
    // the operator selects Setup > Update Mode (see UpdateMode.h) - same
    // shape as enterLegacyMode.
    void begin(
        StageLink::DeviceInfo &deviceInfo,
        StageLink::LabelEditor &labelEditor,
        StageLink::ReliableRadio &radio,
        StageLink::OutputManager &outputManager,
        ShowEngine &showEngine,
        StageLink::OutputCatalog &outputCatalog,
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

    // Confirm held down rather than tapped. Only the name-entry screens
    // use it, where it wipes the name being typed so it can be entered
    // from scratch instead of dialled through character by character.
    // A no-op everywhere else - a hold that does nothing is better than
    // one that does something different on every screen.
    void handleHoldConfirm();

    // Back/cancel, depending on the screen - from a root screen (Show
    // Run/Show Select/Show List/Setup List) this goes up to the Mode
    // Menu; from the Mode Menu itself it does nothing (top of the
    // hierarchy). Fired by the dedicated Back button (GPIO27 - see
    // main.cpp).
    void handleBack();

    // Renders whatever screen is currently active. Call after any
    // handle*() that changed something, and on the normal periodic
    // display refresh.
    void render();

private:
    enum class Screen : uint8_t
    {
        ShowSelect, // Show Mode's picker - which show am I about to run
        ShowRun,
        ModeMenu,
        ShowList,    // Program Mode's show list - which show am I editing
        ShowOptions, // Edit / Rename / Copy / Delete for one show
        CueList,
        CueOptions,   // Edit / Rename / Fade Time / Copy / Move / Delete
        CueFadeEntry, // how long the cue takes to reach its values on GO
        ActionList,
        ActionOptions,   // Edit / Delay / Fade / Move / Delete for one action
        ActionTimeEntry, // the action's delay or fade - see editingActionFade_
        OutputSelect,
        CommandSelect,
        ValueEntry,
        SetupList,
        OutputList,    // Setup > Output Setup - every output on the controller
        OutputOptions, // Rename (type is reported by the catalog, not chosen)
        ControllerLabelEdit,
        NameEdit // renaming a show or a cue - see renameTarget_
    };

    // What an output is, as declared by the catalog - there is no second
    // definition here to drift from it.
    using OutputType = StageLink::OutputCatalog::Type;

    static constexpr uint8_t MAX_STACK_DEPTH = 8;
    static constexpr uint8_t MAX_VALUE_FIELDS = 5;   // sizes editValues_ - the widest fieldsFor() list
    static constexpr uint8_t MAX_STORED_ACTIONS = 4; // widest channelsFor() list - the LED's R/G/B/Brightness
    static constexpr uint8_t MAX_OUTPUTS = 4;        // room for Stepper/Relay once Output Setup exists
    static constexpr uint8_t LABEL_SIZE = 24; // one composed row, e.g. "Q01 Home Position"

    // Longest list any screen draws: one row per cue, or one per stored
    // action (worst case every action a different output), plus up to two
    // trailing entries on the action list.
    static constexpr uint8_t MAX_LIST_ROWS =
        ShowEngine::MAX_CUES > ShowEngine::MAX_ACTIONS_PER_CUE
            ? ShowEngine::MAX_CUES
            : ShowEngine::MAX_ACTIONS_PER_CUE;
    static constexpr uint8_t MAX_TRAILING_ITEMS = 3; // + Add Action, + Add Cue, Done Programming
    static constexpr uint8_t MAX_LIST_ITEMS = MAX_LIST_ROWS + MAX_TRAILING_ITEMS;

    // Which object Screen::NameEdit is currently renaming.
    enum class RenameTarget : uint8_t
    {
        Show,
        Cue,
        Output
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

    OutputType outputTypeOf(uint8_t outputIndex) const;
    const char *const *commandsFor(OutputType type, uint8_t &count) const;
    // Which value fields an action has. Depends on the command, not just
    // the output: an LED's "Color" is hue/white/brightness while its
    // "RGB" is the four raw channels.
    const char *const *fieldsFor(OutputType type, uint8_t commandIndex, uint8_t &count) const;

    // Every OutputManager channel a catalog output owns. Keyed by output
    // rather than by type, so two outputs of the same type each get their
    // own channels. Deliberately *not* parallel to fieldsFor(): an LED
    // owns four channels but is edited as two fields (color plus
    // brightness), so the mapping between them lives in buildActions()/
    // loadValues() rather than being positional.
    const uint8_t *channelsForOutput(uint8_t outputIndex, uint8_t &count) const;

    // Converts the fields currently being edited into the stored actions
    // that represent them, writing up to MAX_STORED_ACTIONS entries and
    // returning how many. This is where one edited action becomes several
    // engine actions - an LED color expands to red/green/blue.
    uint8_t buildActions(uint8_t outputIndex, Action *out) const;

    // The reverse: reads a stored group of actions into
    // loadedChannelValues_ so editing an existing action starts from its
    // real values. Matches by channel, not position. Stops there rather
    // than filling editValues_ - see populateEditFields().
    void loadValues(uint8_t outputIndex, const Action *actions, uint8_t start, uint8_t length);

    // Turns loadedChannelValues_ into the edit fields for whichever
    // command is now selected, or into sensible defaults for a new
    // action. Called once the command is known, since the same stored
    // action maps to different fields under Color than under RGB.
    void populateEditFields();

    // Which catalog output owns an OutputManager channel - the reverse of
    // channelsFor(), used to group stored actions back into rows.
    // Returns OUTPUT_NOT_FOUND for a channel no catalog entry claims.
    static constexpr uint8_t OUTPUT_NOT_FOUND = StageLink::OutputCatalog::NOT_FOUND;
    uint8_t outputIndexForChannel(uint8_t channel) const;

    // Highest value Value Entry lets the encoder reach for a field - the
    // hue step count for an LED hue, 180 for a servo's degrees, 255 for
    // everything else.
    int valueMaxForField(OutputType type, uint8_t commandIndex, uint8_t fieldIndex) const;

    // How far one encoder click moves a field. Levels move in 5s to
    // cross 0-255 in a sensible number of clicks; hue moves one step at a
    // time, white and brightness in 8s (32 clicks end to end).
    int stepForField(OutputType type, uint8_t commandIndex, uint8_t fieldIndex) const;

    // Whether running off the end of a field comes back on the other side
    // - true only for hue, which is a wheel.
    bool fieldWraps(OutputType type, uint8_t commandIndex, uint8_t fieldIndex) const;

    // Number of Action List rows for the cue being edited - stored
    // actions grouped by output, not the raw action count.
    uint8_t uiActionCount() const;

    // Resolves Action List row uiIndex to the range of stored actions it
    // covers. Returns false if the row doesn't exist.
    bool uiActionInfo(uint8_t uiIndex, uint8_t &startOut, uint8_t &lengthOut, uint8_t &outputOut) const;

    // Value stored for a specific OutputManager channel within a group of
    // actions, or 0 if that channel isn't in the group. Matched by
    // channel rather than by position, since a stored group needn't be
    // written in field order - the test show sets LED brightness without
    // setting red/green/blue at all.
    static int32_t valueForChannel(
        const Action *actions, uint8_t start, uint8_t length, uint8_t channel
    );

    // Applies the action currently being edited to real hardware right
    // now, so Value Entry previews live instead of only taking effect
    // once the action is committed and its cue is GOne. Sends every value
    // field of the action (not just the one being edited) through
    // ActionEngine. Safe to call on any screen; a no-op if the selected
    // output has no channels behind it.
    void previewCurrentEdit();

    // Opens the shared LabelEditor on a show or cue name and shows the
    // name-entry screen. Committing writes back through ShowEngine, which
    // clears that object's autoName flag.
    void beginRename(RenameTarget target);
    void commitRename();

    // Rebuilds availableOutputs_ - the outputs Output Select may offer.
    // Outputs already driven by another action in this cue are left out,
    // enforcing one action per output per cue; the output belonging to
    // the action being edited stays in the list so it can be kept.
    void buildAvailableOutputs();

    // Starts editing an Action List row, or a brand new action when
    // length is 0 (start is then where it will be inserted).
    void beginActionFlow(uint8_t start, uint8_t length);

    // Writes the edited action back into ShowEngine, replacing whatever
    // range beginActionFlow() opened, and returns to the Action List.
    void commitActionFlow();

    StackFrame stack_[MAX_STACK_DEPTH];
    uint8_t stackDepth_ = 0;

    // Which cue Program Mode is editing - an index into the selected
    // show's cue list, not a cue number. The show itself is whatever
    // ShowEngine has selected.
    uint8_t currentCueIndex_ = 0;

    // Which show Program Mode's options screen is acting on. Separate
    // from ShowEngine's selected show: opening the options for a show
    // shouldn't load it until Edit is chosen.
    uint8_t optionsShowIndex_ = 0;

    // Which output Setup > Output Setup is acting on.
    uint8_t optionsOutputIndex_ = 0;

    RenameTarget renameTarget_ = RenameTarget::Show;

    // Timing being dialled in, in tenths of a second. Held here rather
    // than written straight through, so backing out of a screen leaves
    // the stored value alone. editingActionFade_ selects which of the
    // action's two times Screen::ActionTimeEntry is editing.
    uint16_t editFadeTenths_ = 0;
    uint16_t editActionTimeTenths_ = 0;
    bool editingActionFade_ = false;

    // Timing carried through an action edit. buildActions() rewrites an
    // action from scratch, so without these an edit would silently reset
    // the delay and fade the operator had set on it.
    // True while a just-created cue is having its fade set, so confirming
    // that goes on into the cue's actions instead of back to the cue list
    // the way editing an existing cue's fade does.
    bool newCueFlow_ = false;

    uint16_t pendingDelayTenths_ = 0;
    uint16_t pendingFadeTenths_ = ACTION_FADE_FROM_CUE;

    // Range of stored actions the open edit flow will replace on commit.
    // editGroupLength_ 0 means "inserting a new action".
    uint8_t editGroupStart_ = 0;
    uint8_t editGroupLength_ = 0;
    uint8_t actionFlowReturnDepth_ = 0;

    uint8_t selectedOutputIndex_ = 0;
    uint8_t selectedCommandIndex_ = 0;
    int editValues_[MAX_VALUE_FIELDS] = {};
    uint8_t editValueCount_ = 0;
    uint8_t editFieldIndex_ = 0;

    // Raw channel values of the action being edited, held between
    // loadValues() and populateEditFields(). hasLoadedValues_ false means
    // a new action, which starts from defaults instead.
    int loadedChannelValues_[MAX_STORED_ACTIONS] = {};
    bool hasLoadedValues_ = false;

    // Catalog indices Output Select is currently offering, and how many.
    // Rebuilt whenever an action edit begins - see buildAvailableOutputs().
    uint8_t availableOutputs_[MAX_OUTPUTS] = {};
    uint8_t availableOutputCount_ = 0;

    // Backing storage for whichever list render() is currently drawing -
    // itemPointers_ points into listLabels_ for rows that need a composed
    // string (a cue's "Q01 Name", an action's value summary). Both are rebuilt fresh each render() call and
    // never read across calls.
    const char *itemPointers_[MAX_LIST_ITEMS];
    char listLabels_[MAX_LIST_ITEMS][LABEL_SIZE];

    StageLink::DeviceInfo *deviceInfo_ = nullptr;
    StageLink::LabelEditor *labelEditor_ = nullptr;
    StageLink::ReliableRadio *radio_ = nullptr;
    StageLink::OutputManager *outputManager_ = nullptr;
    ShowEngine *showEngine_ = nullptr;
    StageLink::OutputCatalog *outputCatalog_ = nullptr;

    // Used only by previewCurrentEdit() - ShowEngine has its own instance
    // for cue playback, and reaching into it would couple the editor to
    // playback state for no benefit. ActionEngine is stateless, so a
    // second one costs nothing.
    ActionEngine previewEngine_;

    void (*commitUnitLabel_)() = nullptr;
    void (*enterLegacyMode_)() = nullptr;
    void (*enterUpdateMode_)() = nullptr;
};
