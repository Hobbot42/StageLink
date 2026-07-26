#include "GuiController.h"

#include <Arduino.h>
#include <cstdio>
#include "Display.h"

namespace
{
    // Fake output catalog - see GuiController.h class comment. Index
    // here is what Action::outputIndex refers to throughout.
    constexpr const char *OUTPUT_LABELS[] = { "LED 1", "Servo 1", "Servo 2", "Stepper 1", "Relay 1" };
    constexpr uint8_t OUTPUT_COUNT = 5;

    constexpr const char *LED_COMMANDS[] = { "Color", "Brightness", "Fade", "Effect" };
    constexpr const char *SERVO_COMMANDS[] = { "Position", "Fade" };
    constexpr const char *STEPPER_COMMANDS[] = { "Position", "Speed", "Enable" };
    constexpr const char *RELAY_COMMANDS[] = { "On", "Off" };

    // Grouped by output type rather than by individual command - keeps
    // this prototype's value-entry screens simple (see GuiController.h);
    // a real implementation would likely vary fields per command too.
    constexpr const char *LED_FIELDS[] = { "RED", "GREEN", "BLUE", "WHITE", "BRIGHTNESS" };
    constexpr const char *SERVO_FIELDS[] = { "POSITION" };
    constexpr const char *STEPPER_FIELDS[] = { "VALUE" };

    constexpr const char *MODE_MENU_ITEMS[] = { "Show Mode", "Program Mode", "Setup Mode" };
    constexpr const char *SETUP_ITEMS[] = { "Unit Label", "Diagnostics", "Update Mode" };

    uint8_t wrapIndex(uint8_t current, int step, uint8_t count)
    {
        if (count == 0)
        {
            return 0;
        }

        int next = static_cast<int>(current) + step;
        int wrapped = ((next % static_cast<int>(count)) + static_cast<int>(count)) % static_cast<int>(count);
        return static_cast<uint8_t>(wrapped);
    }

    uint8_t clampIndex(uint8_t current, int step, uint8_t count)
    {
        if (count == 0)
        {
            return 0;
        }

        int next = static_cast<int>(current) + step;
        if (next < 0)
        {
            next = 0;
        }
        if (next >= static_cast<int>(count))
        {
            next = count - 1;
        }
        return static_cast<uint8_t>(next);
    }
}

void GuiController::begin(
    StageLink::DeviceInfo &deviceInfo,
    StageLink::LabelEditor &labelEditor,
    StageLink::ReliableRadio &radio,
    StageLink::OutputManager &outputManager,
    ShowEngine &showEngine,
    void (*commitUnitLabel)(),
    void (*enterLegacyMode)(),
    void (*enterUpdateMode)()
)
{
    deviceInfo_ = &deviceInfo;
    labelEditor_ = &labelEditor;
    radio_ = &radio;
    outputManager_ = &outputManager;
    showEngine_ = &showEngine;
    commitUnitLabel_ = commitUnitLabel;
    enterLegacyMode_ = enterLegacyMode;
    enterUpdateMode_ = enterUpdateMode;

    seedFakeData();

    stackDepth_ = 1;
    stack_[0] = { Screen::ShowRun, 0 };
}

void GuiController::seedFakeData()
{
    showCount_ = 0;

    // Dragon Show - the main test show, with a couple of pre-populated
    // actions so Edit Cue/Edit Action aren't empty on first look.
    Show &dragon = shows_[showCount_++];
    snprintf(dragon.label, LABEL_SIZE, "DRAGON SHOW");
    dragon.cueCount = 5;
    for (uint8_t i = 0; i < dragon.cueCount; ++i)
    {
        snprintf(dragon.cues[i].label, LABEL_SIZE, "Cue %02u", i + 1);
        dragon.cues[i].actionCount = 0;
        dragon.cues[i].timeSeconds = 3.0f;
    }

    GuiController::Action &a1 = dragon.cues[1].actions[dragon.cues[1].actionCount++];
    a1.outputIndex = 0; // LED 1
    a1.commandIndex = 0; // Color
    a1.values[0] = 255;
    a1.values[1] = 0;
    a1.values[2] = 0;
    a1.values[3] = 0;
    a1.values[4] = 255;
    a1.valueCount = 5;

    GuiController::Action &a2 = dragon.cues[3].actions[dragon.cues[3].actionCount++];
    a2.outputIndex = 1; // Servo 1
    a2.commandIndex = 0; // Position
    a2.values[0] = 90;
    a2.valueCount = 1;

    // Fog Loop - empty show, tests the "0 cues" state.
    Show &fog = shows_[showCount_++];
    snprintf(fog.label, LABEL_SIZE, "FOG LOOP");
    fog.cueCount = 0;

    // Light Demo - one bare cue, tests a short list.
    Show &light = shows_[showCount_++];
    snprintf(light.label, LABEL_SIZE, "LIGHT DEMO");
    light.cueCount = 1;
    snprintf(light.cues[0].label, LABEL_SIZE, "Cue 01");
    light.cues[0].actionCount = 0;
    light.cues[0].timeSeconds = 2.0f;
}

GuiController::OutputType GuiController::outputTypeOf(uint8_t outputIndex) const
{
    switch (outputIndex)
    {
        case 0:
            return OutputType::Led; // LED 1
        case 1:
        case 2:
            return OutputType::Servo; // Servo 1/2
        case 3:
            return OutputType::Stepper; // Stepper 1
        default:
            return OutputType::Relay; // Relay 1
    }
}

const char *const *GuiController::commandsFor(OutputType type, uint8_t &count) const
{
    switch (type)
    {
        case OutputType::Led:
            count = 4;
            return LED_COMMANDS;
        case OutputType::Servo:
            count = 2;
            return SERVO_COMMANDS;
        case OutputType::Stepper:
            count = 3;
            return STEPPER_COMMANDS;
        default:
            count = 2;
            return RELAY_COMMANDS;
    }
}

const char *const *GuiController::fieldsFor(OutputType type, uint8_t &count) const
{
    switch (type)
    {
        case OutputType::Led:
            count = 5;
            return LED_FIELDS;
        case OutputType::Servo:
            count = 1;
            return SERVO_FIELDS;
        case OutputType::Stepper:
            count = 1;
            return STEPPER_FIELDS;
        default:
            count = 0; // Relay - command is the whole action, no values
            return nullptr;
    }
}

void GuiController::pushScreen(Screen screen)
{
    if (stackDepth_ >= MAX_STACK_DEPTH)
    {
        return;
    }

    stack_[stackDepth_] = { screen, 0 };
    stackDepth_++;
}

void GuiController::popScreen()
{
    if (stackDepth_ > 1)
    {
        stackDepth_--;
    }
}

GuiController::Screen GuiController::currentScreen() const
{
    return stack_[stackDepth_ - 1].screen;
}

uint8_t &GuiController::currentSelection()
{
    return stack_[stackDepth_ - 1].selection;
}

void GuiController::beginActionFlow(bool editingExisting, uint8_t actionIndex)
{
    editingExistingAction_ = editingExisting;
    currentActionIndex_ = actionIndex;
    actionFlowReturnDepth_ = stackDepth_;
    editCueTime_ = shows_[currentShowIndex_].cues[currentCueIndex_].timeSeconds;

    if (editingExisting)
    {
        const Action &action = shows_[currentShowIndex_].cues[currentCueIndex_].actions[actionIndex];
        selectedOutputIndex_ = action.outputIndex;
        selectedCommandIndex_ = action.commandIndex;
        for (uint8_t i = 0; i < action.valueCount; ++i)
        {
            editValues_[i] = action.values[i];
        }
        editValueCount_ = action.valueCount;
    }
    else
    {
        // selectedOutputIndex_/selectedCommandIndex_ deliberately left
        // as whatever was last used, not reset to LED 1/Color - "use
        // last-used values as defaults" per the UI rule, so repeated
        // Add Action doesn't force re-selecting from scratch each time.
        for (uint8_t i = 0; i < 5; ++i)
        {
            editValues_[i] = 128;
        }
        editValueCount_ = 0;
    }

    editFieldIndex_ = 0;
    pushScreen(Screen::OutputSelect);
    currentSelection() = selectedOutputIndex_;
}

void GuiController::commitActionFlow()
{
    Cue &cue = shows_[currentShowIndex_].cues[currentCueIndex_];
    Action &action = cue.actions[currentActionIndex_];

    action.outputIndex = selectedOutputIndex_;
    action.commandIndex = selectedCommandIndex_;
    action.valueCount = editValueCount_;
    for (uint8_t i = 0; i < editValueCount_; ++i)
    {
        action.values[i] = editValues_[i];
    }

    if (!editingExistingAction_ && cue.actionCount <= currentActionIndex_)
    {
        cue.actionCount = currentActionIndex_ + 1;
    }

    cue.timeSeconds = editCueTime_;

    // Jumps straight back to Action List, discarding the Output Select/
    // Command Select/Value Entry/Cue Time frames pushed during this
    // flow - a plain popScreen() would only remove one at a time.
    stackDepth_ = actionFlowReturnDepth_;
}

void GuiController::handleRotate(int direction)
{
    int step = direction > 0 ? 1 : -1;

    switch (currentScreen())
    {
        case Screen::ShowRun:
            // Moves ShowEngine's selected cue - see ShowEngine.h. Never
            // touches the current cue; only go() (handlePress()) does.
            if (step > 0)
            {
                showEngine_->nextCue();
            }
            else
            {
                showEngine_->previousCue();
            }
            break;

        case Screen::ModeMenu:
            currentSelection() = wrapIndex(currentSelection(), step, 3);
            break;

        case Screen::ShowList:
            currentSelection() = wrapIndex(currentSelection(), step, showCount_ + 1);
            break;

        case Screen::CueList:
            currentSelection() = wrapIndex(
                currentSelection(), step, shows_[currentShowIndex_].cueCount + 1
            );
            break;

        case Screen::ActionList:
            currentSelection() = wrapIndex(
                currentSelection(), step,
                shows_[currentShowIndex_].cues[currentCueIndex_].actionCount + 1
            );
            break;

        case Screen::OutputSelect:
            currentSelection() = wrapIndex(currentSelection(), step, OUTPUT_COUNT);
            break;

        case Screen::CommandSelect:
        {
            uint8_t count = 0;
            commandsFor(outputTypeOf(selectedOutputIndex_), count);
            currentSelection() = wrapIndex(currentSelection(), step, count);
            break;
        }

        case Screen::ValueEntry:
            editValues_[editFieldIndex_] = constrain(editValues_[editFieldIndex_] + step * 5, 0, 255);
            break;

        case Screen::CueTimeEntry:
            editCueTime_ = constrain(editCueTime_ + step * 0.5f, 0.0f, 60.0f);
            break;

        case Screen::SetupList:
            currentSelection() = wrapIndex(currentSelection(), step, 3);
            break;

        case Screen::UnitLabelEdit:
            labelEditor_->stepChar(step);
            break;
    }
}

void GuiController::handlePress()
{
    switch (currentScreen())
    {
        case Screen::ShowRun:
            // GO - ShowEngine::go() moves the selected cue into current
            // (bracketed) and advances the selection past it, then
            // executeCurrentCue() sends that cue's actions to
            // OutputManager (see ShowEngine.h/Action.h) - the first real
            // link from a cue to physical output.
            showEngine_->go();
            showEngine_->executeCurrentCue(*outputManager_);
            break;

        case Screen::ModeMenu:
            if (currentSelection() == 0) // Show Mode
            {
                stackDepth_ = 1;
                stack_[0] = { Screen::ShowRun, 0 };
            }
            else if (currentSelection() == 1) // Program Mode
            {
                stackDepth_ = 1;
                stack_[0] = { Screen::ShowList, 0 };
            }
            else // Setup Mode
            {
                stackDepth_ = 1;
                stack_[0] = { Screen::SetupList, 0 };
            }
            break;

        case Screen::ShowList:
            if (currentSelection() == showCount_) // "+ New Show"
            {
                if (showCount_ < MAX_SHOWS)
                {
                    Show &show = shows_[showCount_];
                    snprintf(show.label, LABEL_SIZE, "NEW SHOW %u", showCount_ + 1);
                    show.cueCount = 0;
                    currentShowIndex_ = showCount_;
                    showCount_++;
                    pushScreen(Screen::CueList);
                }
            }
            else
            {
                currentShowIndex_ = currentSelection();
                pushScreen(Screen::CueList);
            }
            break;

        case Screen::CueList:
        {
            Show &show = shows_[currentShowIndex_];
            if (currentSelection() == show.cueCount) // "+ Add Cue"
            {
                if (show.cueCount < MAX_CUES)
                {
                    Cue &cue = show.cues[show.cueCount];
                    snprintf(cue.label, LABEL_SIZE, "Cue %02u", show.cueCount + 1);
                    cue.actionCount = 0;
                    cue.timeSeconds = 3.0f;
                    currentCueIndex_ = show.cueCount;
                    show.cueCount++;
                    pushScreen(Screen::ActionList);
                }
            }
            else
            {
                currentCueIndex_ = currentSelection();
                pushScreen(Screen::ActionList);
            }
            break;
        }

        case Screen::ActionList:
        {
            Cue &cue = shows_[currentShowIndex_].cues[currentCueIndex_];
            if (currentSelection() == cue.actionCount) // "+ Add Action"
            {
                if (cue.actionCount < MAX_ACTIONS)
                {
                    beginActionFlow(false, cue.actionCount);
                }
            }
            else
            {
                beginActionFlow(true, currentSelection());
            }
            break;
        }

        case Screen::OutputSelect:
            selectedOutputIndex_ = currentSelection();
            pushScreen(Screen::CommandSelect);
            break;

        case Screen::CommandSelect:
        {
            selectedCommandIndex_ = currentSelection();

            uint8_t fieldCount = 0;
            fieldsFor(outputTypeOf(selectedOutputIndex_), fieldCount);
            editValueCount_ = fieldCount;
            editFieldIndex_ = 0;

            if (fieldCount == 0)
            {
                // Relay - the command itself is the whole action.
                pushScreen(Screen::CueTimeEntry);
            }
            else
            {
                pushScreen(Screen::ValueEntry);
            }
            break;
        }

        case Screen::ValueEntry:
            if (editFieldIndex_ + 1 < editValueCount_)
            {
                editFieldIndex_++;
            }
            else
            {
                pushScreen(Screen::CueTimeEntry);
            }
            break;

        case Screen::CueTimeEntry:
            commitActionFlow();
            break;

        case Screen::SetupList:
            if (currentSelection() == 0) // Unit Label
            {
                labelEditor_->begin(deviceInfo_->unitLabel);
                pushScreen(Screen::UnitLabelEdit);
            }
            else if (currentSelection() == 1) // Diagnostics
            {
                if (enterLegacyMode_ != nullptr)
                {
                    enterLegacyMode_();
                }
            }
            else if (enterUpdateMode_ != nullptr) // Update Mode
            {
                enterUpdateMode_();
            }
            break;

        case Screen::UnitLabelEdit:
            if (labelEditor_->confirmChar())
            {
                if (commitUnitLabel_ != nullptr)
                {
                    commitUnitLabel_();
                }
                popScreen();
            }
            break;
    }
}

void GuiController::handleHold()
{
    switch (currentScreen())
    {
        case Screen::ShowRun:
        case Screen::ShowList:
        case Screen::SetupList:
            // Root screen for the current mode - nothing left to pop
            // within the mode itself, so Back goes up to the mode
            // switcher instead of doing nothing.
            pushScreen(Screen::ModeMenu);
            break;

        case Screen::ModeMenu:
            // Top of the navigation hierarchy - Back does nothing here,
            // it doesn't cycle back down into whichever mode was
            // showing before.
            break;

        case Screen::UnitLabelEdit:
            labelEditor_->back();
            if (!labelEditor_->isActive())
            {
                popScreen();
            }
            break;

        default:
            popScreen();
            break;
    }
}

void GuiController::render()
{
    switch (currentScreen())
    {
        case Screen::ShowRun:
        {
            // Real ShowEngine data, not shows_/the PROGRAM MODE fake
            // data - see the class comment.
            StageLink::RadioDiagnostics diagnostics = radio_->diagnostics();

            Display::showGuiShowRun(
                deviceInfo_->unitLabel,
                showEngine_->getShowName(),
                showEngine_->getCurrentCue(),
                showEngine_->getCueName(showEngine_->getCurrentCue()),
                showEngine_->getSelectedCue(),
                showEngine_->getCueName(showEngine_->getSelectedCue()),
                diagnostics.peerOnline,
                diagnostics.rssiAvailable,
                diagnostics.rssi
            );
            break;
        }

        case Screen::ModeMenu:
            Display::showGuiList("MODE", nullptr, MODE_MENU_ITEMS, 3, currentSelection());
            break;

        case Screen::ShowList:
            for (uint8_t i = 0; i < showCount_; ++i)
            {
                itemPointers_[i] = shows_[i].label;
            }
            itemPointers_[showCount_] = "+ New Show";
            Display::showGuiList("SHOW MENU", nullptr, itemPointers_, showCount_ + 1, currentSelection());
            break;

        case Screen::CueList:
        {
            Show &show = shows_[currentShowIndex_];
            for (uint8_t i = 0; i < show.cueCount; ++i)
            {
                itemPointers_[i] = show.cues[i].label;
            }
            itemPointers_[show.cueCount] = "+ Add Cue";
            Display::showGuiList("EDIT SHOW", show.label, itemPointers_, show.cueCount + 1, currentSelection());
            break;
        }

        case Screen::ActionList:
        {
            Cue &cue = shows_[currentShowIndex_].cues[currentCueIndex_];
            for (uint8_t i = 0; i < cue.actionCount; ++i)
            {
                itemPointers_[i] = OUTPUT_LABELS[cue.actions[i].outputIndex];
            }
            itemPointers_[cue.actionCount] = "+ Add Action";
            Display::showGuiList("EDIT CUE", cue.label, itemPointers_, cue.actionCount + 1, currentSelection());
            break;
        }

        case Screen::OutputSelect:
            Display::showGuiList("SELECT OUTPUT", nullptr, OUTPUT_LABELS, OUTPUT_COUNT, currentSelection());
            break;

        case Screen::CommandSelect:
        {
            uint8_t count = 0;
            const char *const *commands = commandsFor(outputTypeOf(selectedOutputIndex_), count);
            Display::showGuiList(
                "SELECT COMMAND", OUTPUT_LABELS[selectedOutputIndex_], commands, count, currentSelection()
            );
            break;
        }

        case Screen::ValueEntry:
        {
            uint8_t fieldCount = 0;
            const char *const *fields = fieldsFor(outputTypeOf(selectedOutputIndex_), fieldCount);

            uint8_t cmdCount = 0;
            const char *const *commands = commandsFor(outputTypeOf(selectedOutputIndex_), cmdCount);

            char context[24];
            snprintf(
                context, sizeof(context), "%s/%s",
                OUTPUT_LABELS[selectedOutputIndex_], commands[selectedCommandIndex_]
            );

            char valueText[8];
            snprintf(valueText, sizeof(valueText), "%d", editValues_[editFieldIndex_]);

            Display::showGuiValueEntry(
                "EDIT ACTION", context, fields[editFieldIndex_], valueText, editFieldIndex_, fieldCount
            );
            break;
        }

        case Screen::CueTimeEntry:
        {
            char valueText[12];
            snprintf(valueText, sizeof(valueText), "%.1f sec", static_cast<double>(editCueTime_));
            Display::showGuiValueEntry(
                "CUE TIME", shows_[currentShowIndex_].cues[currentCueIndex_].label, "TIME", valueText, 0, 1
            );
            break;
        }

        case Screen::SetupList:
            Display::showGuiList("SETUP", nullptr, SETUP_ITEMS, 3, currentSelection());
            break;

        case Screen::UnitLabelEdit:
            Display::showUnitLabelEdit(labelEditor_->buffer(), labelEditor_->cursor());
            break;
    }
}
