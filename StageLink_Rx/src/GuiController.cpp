#include "GuiController.h"

#include <Arduino.h>
#include <cstdio>
#include "Display.h"

namespace
{
    // Counts derived from the arrays rather than written out by hand.
    // These are handed straight to the display as "how many entries to
    // read", so a count that outlives an edit to its array walks off the
    // end and renders whatever follows it as a string.
    template <typename T, size_t N>
    constexpr uint8_t itemCount(const T (&)[N])
    {
        return static_cast<uint8_t>(N);
    }

    // Output catalog - index here is what selectedOutputIndex_ refers to
    // throughout. Only the outputs this board actually drives are listed,
    // so Value Entry's live preview always has real hardware behind it.
    // Stepper/Relay stay in OutputType for when Setup > Output Setup can
    // define outputs properly - at that point this hardcoded catalog is
    // what it replaces. Deliberately not named OUT-01/OUT-02 yet: the
    // physical numbering is Output Setup's to assign, and guessing it
    // here would bake in a wrong mapping.
    constexpr const char *OUTPUT_LABELS[] = { "LED", "SERVO" };
    constexpr uint8_t OUTPUT_COUNT = itemCount(OUTPUT_LABELS);

    // OutputManager channel each value field drives, in the same order as
    // the matching *_FIELDS array below. Must match RX main.cpp's
    // OUTPUT_CHANNEL_* constants - that file's anonymous namespace isn't
    // reachable from here, so this is kept in sync by hand, exactly like
    // ShowEngine.cpp's TEST_SHOW_*_OUTPUT_ID constants.
    constexpr uint8_t LED_CHANNELS[] = { 3, 4, 5, 2 }; // RED, GREEN, BLUE, BRIGHTNESS
    constexpr uint8_t SERVO_CHANNELS[] = { 1 };        // POSITION

    // Two ways to set the same LED: the color picker (hue/white/
    // brightness, one dial at a time) or raw RGB channels. They write
    // identical stored actions - only the controls differ.
    constexpr const char *LED_COMMANDS[] = { "Color", "RGB" };
    constexpr uint8_t LED_COMMAND_COLOR = 0;
    constexpr uint8_t LED_COMMAND_RGB = 1;
    // Only Position - "Fade" behaved identically (fields are per output
    // type, and ActionEngine implements Level either way), so it was a
    // second entry that did the same thing. A real fade belongs with
    // action timing, not as a duplicate command. With one command left,
    // the Command screen is skipped entirely - see OutputSelect.
    constexpr const char *SERVO_COMMANDS[] = { "Position" };
    constexpr const char *STEPPER_COMMANDS[] = { "Position", "Speed", "Enable" };
    constexpr const char *RELAY_COMMANDS[] = { "On", "Off" };


    // Grouped by output type rather than by individual command - keeps
    // value entry simple (see GuiController.h); a real implementation
    // would likely vary fields per command too.
    //
    // Color command - one encoder, three dials stepped through with the
    // button. Hue picks the color, White desaturates it toward white, and
    // Brightness sets how hard the strip is driven.
    constexpr const char *LED_COLOR_FIELDS[] = { "HUE", "WHITE", "BRIGHTNESS" };
    constexpr uint8_t LED_COLOR_FIELD_HUE = 0;
    constexpr uint8_t LED_COLOR_FIELD_WHITE = 1;
    constexpr uint8_t LED_COLOR_FIELD_BRIGHTNESS = 2;

    // RGB command - the raw channels, for when a specific mix is wanted
    // rather than a point on the wheel.
    constexpr const char *LED_RGB_FIELDS[] = { "RED", "GREEN", "BLUE", "BRIGHTNESS" };

    constexpr const char *SERVO_FIELDS[] = { "POSITION" };
    constexpr const char *STEPPER_FIELDS[] = { "VALUE" };

    // Channel positions within LED_CHANNELS.
    constexpr uint8_t LED_CHANNEL_RED = 0;
    constexpr uint8_t LED_CHANNEL_GREEN = 1;
    constexpr uint8_t LED_CHANNEL_BLUE = 2;
    constexpr uint8_t LED_CHANNEL_BRIGHTNESS = 3;

    // 24 hues at 15 degrees apart, so pure red (0), green (120) and blue
    // (240) all land exactly on a step rather than being approached from
    // either side. Wraps in both directions - see fieldWraps().
    constexpr uint8_t HUE_STEP_COUNT = 24;
    constexpr int HUE_STEP_DEGREES = 360 / HUE_STEP_COUNT;

    // WHITE is how much white is mixed in: 0 is the fully saturated hue,
    // 255 is pure white. That is the inverse of the saturation HSV wants,
    // so the two are converted at the edges (buildActions()/
    // populateEditFields()) and the dial reads the way it's labelled.
    constexpr int SATURATION_MAX = 255;

    // Standard HSV->RGB. Saturation and value arrive 0-255; hue in
    // degrees. Value is kept at full here and real brightness is left to
    // the LED's own brightness channel - baking it into RGB as well would
    // attenuate twice and throw away color resolution at low levels.
    void hsvToRgb(int hueDegrees, int saturation, int value, int &red, int &green, int &blue)
    {
        const float h = static_cast<float>(((hueDegrees % 360) + 360) % 360) / 60.0f;
        const float s = static_cast<float>(saturation) / 255.0f;
        const float v = static_cast<float>(value) / 255.0f;

        const int sector = static_cast<int>(h) % 6;
        const float f = h - static_cast<float>(static_cast<int>(h));

        const float p = v * (1.0f - s);
        const float q = v * (1.0f - s * f);
        const float t = v * (1.0f - s * (1.0f - f));

        float r = v;
        float g = v;
        float b = v;

        switch (sector)
        {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }

        red = static_cast<int>(r * 255.0f + 0.5f);
        green = static_cast<int>(g * 255.0f + 0.5f);
        blue = static_cast<int>(b * 255.0f + 0.5f);
    }

    // The reverse, used when an existing action is opened for editing so
    // the dials start where the stored color actually is. Approximate by
    // nature: RGB holds more colors than 24 hue steps can name, so the
    // hue lands on the nearest step.
    void rgbToHueSaturation(int red, int green, int blue, int &hueDegrees, int &saturation)
    {
        const int maxChannel = max(red, max(green, blue));
        const int minChannel = min(red, min(green, blue));
        const int delta = maxChannel - minChannel;

        if (maxChannel <= 0 || delta == 0)
        {
            // Black or a pure grey - no hue to recover, so leave the dial
            // at red and report it as fully white.
            hueDegrees = 0;
            saturation = 0;
            return;
        }

        saturation = delta * 255 / maxChannel;

        float hue = 0.0f;
        if (maxChannel == red)
        {
            hue = 60.0f * static_cast<float>(green - blue) / static_cast<float>(delta);
        }
        else if (maxChannel == green)
        {
            hue = 120.0f + 60.0f * static_cast<float>(blue - red) / static_cast<float>(delta);
        }
        else
        {
            hue = 240.0f + 60.0f * static_cast<float>(red - green) / static_cast<float>(delta);
        }

        if (hue < 0.0f)
        {
            hue += 360.0f;
        }

        hueDegrees = static_cast<int>(hue + 0.5f) % 360;
    }

    constexpr const char *MODE_MENU_ITEMS[] = { "Show Mode", "Program Mode", "Setup Mode" };
    constexpr const char *SETUP_ITEMS[] = { "Controller Label", "Diagnostics", "Update Mode" };

    // Shown in place of a list that has nothing in it yet - a unit ships
    // with no shows, so an empty Show Mode picker is a normal state, not
    // an error.
    constexpr const char *NO_SHOWS_ITEM = "(no shows)";

    // Last entry on the action list. Programming runs deep - show, cue,
    // action - so this is a single step back out to the top rather than
    // pressing Back once per level.
    constexpr const char *DONE_PROGRAMMING_ITEM = "Done Programming";

    // What pressing an existing Action List row offers. Reordering and
    // deleting live here rather than on a gesture of their own, so the
    // encoder/press/hold convention stays the same on every screen.
    constexpr const char *ACTION_OPTIONS[] = { "Edit", "Move Up", "Move Down", "Delete" };

    // "Edit" opens the cue's action list, which is what pressing the cue
    // used to do directly.
    constexpr const char *CUE_OPTIONS[] = {
        "Edit", "Fade Time", "Rename", "Copy", "Move Up", "Move Down", "Delete"
    };
    constexpr uint8_t CUE_OPTION_COUNT = itemCount(CUE_OPTIONS);
    constexpr uint8_t CUE_OPTION_EDIT = 0;
    constexpr uint8_t CUE_OPTION_FADE = 1;
    constexpr uint8_t CUE_OPTION_RENAME = 2;
    constexpr uint8_t CUE_OPTION_COPY = 3;
    constexpr uint8_t CUE_OPTION_MOVE_UP = 4;
    constexpr uint8_t CUE_OPTION_MOVE_DOWN = 5;
    constexpr uint8_t CUE_OPTION_DELETE = 6;

    // Encoder step for the cue fade, in tenths of a second. 0.1s steps
    // stay precise where fades are usually set, but 99.9s would be 999
    // clicks away - so past 10s it moves a second at a time.
    constexpr uint16_t FADE_FINE_LIMIT_TENTHS = 100; // 10.0s
    constexpr int FADE_FINE_STEP = 1;                // 0.1s
    constexpr int FADE_COARSE_STEP = 10;             // 1.0s

    // A show has no ordering operations - shows aren't executed in
    // sequence, so Move Up/Down would order nothing.
    constexpr const char *SHOW_OPTIONS[] = { "Edit", "Rename", "Copy", "Delete" };
    constexpr uint8_t SHOW_OPTION_COUNT = itemCount(SHOW_OPTIONS);
    constexpr uint8_t SHOW_OPTION_EDIT = 0;
    constexpr uint8_t SHOW_OPTION_RENAME = 1;
    constexpr uint8_t SHOW_OPTION_COPY = 2;
    constexpr uint8_t SHOW_OPTION_DELETE = 3;
    constexpr uint8_t ACTION_OPTION_COUNT = itemCount(ACTION_OPTIONS);
    constexpr uint8_t ACTION_OPTION_EDIT = 0;
    constexpr uint8_t ACTION_OPTION_MOVE_UP = 1;
    constexpr uint8_t ACTION_OPTION_MOVE_DOWN = 2;
    constexpr uint8_t ACTION_OPTION_DELETE = 3;

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

    stackDepth_ = 1;

    if (showEngine_->getShowCount() > 0)
    {
        // Comes up on the first show, already running its cue 1, so the
        // unit is ready to GO out of the box rather than waiting behind a
        // menu. The picker is still one Back away.
        showEngine_->selectShow(0);
        showEngine_->executeCurrentCue(outputManager);
        stack_[0] = { Screen::ShowRun, 0 };
    }
    else
    {
        // Nothing to run - the picker is the honest landing screen, and
        // it says so (see NO_SHOWS_ITEM).
        stack_[0] = { Screen::ShowSelect, 0 };
    }
}

GuiController::OutputType GuiController::outputTypeOf(uint8_t outputIndex) const
{
    switch (outputIndex)
    {
        case 0:
            return OutputType::Led;
        default:
            return OutputType::Servo;
    }
}

const uint8_t *GuiController::channelsFor(OutputType type, uint8_t &count) const
{
    switch (type)
    {
        case OutputType::Led:
            count = 4;
            return LED_CHANNELS;
        case OutputType::Servo:
            count = 1;
            return SERVO_CHANNELS;
        default:
            count = 0;
            return nullptr; // Stepper/Relay - no hardware behind them yet
    }
}

uint8_t GuiController::outputIndexForChannel(uint8_t channel) const
{
    for (uint8_t outputIndex = 0; outputIndex < OUTPUT_COUNT; ++outputIndex)
    {
        uint8_t channelCount = 0;
        const uint8_t *channels = channelsFor(outputTypeOf(outputIndex), channelCount);

        if (channels == nullptr)
        {
            continue;
        }

        for (uint8_t i = 0; i < channelCount; ++i)
        {
            if (channels[i] == channel)
            {
                return outputIndex;
            }
        }
    }

    return OUTPUT_NOT_FOUND;
}

uint8_t GuiController::buildActions(OutputType type, Action *out) const
{
    uint8_t channelCount = 0;
    const uint8_t *channels = channelsFor(type, channelCount);

    if (channels == nullptr)
    {
        return 0;
    }

    if (type == OutputType::Led)
    {
        int red = 0;
        int green = 0;
        int blue = 0;
        int brightness = 0;

        if (selectedCommandIndex_ == LED_COMMAND_RGB)
        {
            red = editValues_[LED_CHANNEL_RED];
            green = editValues_[LED_CHANNEL_GREEN];
            blue = editValues_[LED_CHANNEL_BLUE];
            brightness = editValues_[LED_CHANNEL_BRIGHTNESS];
        }
        else
        {
            // Hue index -> degrees, at full value: the brightness dial
            // drives the LED's own brightness channel rather than being
            // multiplied into RGB here. See hsvToRgb().
            hsvToRgb(
                editValues_[LED_COLOR_FIELD_HUE] * HUE_STEP_DEGREES,
                SATURATION_MAX - editValues_[LED_COLOR_FIELD_WHITE],
                255,
                red, green, blue
            );
            brightness = editValues_[LED_COLOR_FIELD_BRIGHTNESS];
        }

        const int values[] = { red, green, blue, brightness };

        for (uint8_t i = 0; i < channelCount; ++i)
        {
            out[i].outputId = channels[i];
            out[i].command = ActionCommand::Level;
            out[i].value = values[i];
        }

        return channelCount;
    }

    // Everything else is one field per channel, in order.
    uint8_t fieldCount = 0;
    fieldsFor(type, selectedCommandIndex_, fieldCount);

    uint8_t count = 0;
    for (uint8_t i = 0; i < fieldCount && i < channelCount; ++i)
    {
        out[count].outputId = channels[i];
        out[count].command = ActionCommand::Level;
        out[count].value = editValues_[i];
        count++;
    }

    return count;
}

void GuiController::loadValues(
    OutputType type, const Action *actions, uint8_t start, uint8_t length
)
{
    uint8_t channelCount = 0;
    const uint8_t *channels = channelsFor(type, channelCount);

    if (channels == nullptr)
    {
        hasLoadedValues_ = false;
        return;
    }

    // Kept as raw channel values rather than converted to fields straight
    // away: which fields those become depends on the command, and the
    // operator doesn't choose that until the Command screen. See
    // populateEditFields().
    for (uint8_t i = 0; i < channelCount && i < MAX_STORED_ACTIONS; ++i)
    {
        loadedChannelValues_[i] = valueForChannel(actions, start, length, channels[i]);
    }

    hasLoadedValues_ = true;
}

void GuiController::populateEditFields()
{
    const OutputType type = outputTypeOf(selectedOutputIndex_);

    uint8_t fieldCount = 0;
    fieldsFor(type, selectedCommandIndex_, fieldCount);
    editValueCount_ = fieldCount;
    editFieldIndex_ = 0;

    if (!hasLoadedValues_)
    {
        // A new action starts from where the output actually is - the
        // servo's current position, the LED's current color and level -
        // so opening Value Entry doesn't jump the rig to an arbitrary
        // default before the operator has touched anything.
        uint8_t channelCount = 0;
        const uint8_t *channels = channelsFor(type, channelCount);

        for (uint8_t i = 0; i < channelCount && i < MAX_STORED_ACTIONS; ++i)
        {
            loadedChannelValues_[i] =
                outputManager_ == nullptr ? 0 : outputManager_->lastValue(channels[i]);
        }

        // Falls through to the same conversion a stored action uses, so
        // there's one path from channel values to dials.
        hasLoadedValues_ = true;
    }

    if (type == OutputType::Led)
    {
        if (selectedCommandIndex_ == LED_COMMAND_RGB)
        {
            editValues_[LED_CHANNEL_RED] = loadedChannelValues_[LED_CHANNEL_RED];
            editValues_[LED_CHANNEL_GREEN] = loadedChannelValues_[LED_CHANNEL_GREEN];
            editValues_[LED_CHANNEL_BLUE] = loadedChannelValues_[LED_CHANNEL_BLUE];
            editValues_[LED_CHANNEL_BRIGHTNESS] = loadedChannelValues_[LED_CHANNEL_BRIGHTNESS];
            return;
        }

        // Recovering hue/white from RGB is approximate - RGB holds more
        // colors than 24 hue steps can name, so this lands on the nearest
        // step. Switching an action between RGB and Color is therefore a
        // lossy conversion, which is why both commands exist.
        int hueDegrees = 0;
        int saturation = 0;
        rgbToHueSaturation(
            loadedChannelValues_[LED_CHANNEL_RED],
            loadedChannelValues_[LED_CHANNEL_GREEN],
            loadedChannelValues_[LED_CHANNEL_BLUE],
            hueDegrees, saturation
        );

        editValues_[LED_COLOR_FIELD_HUE] =
            ((hueDegrees + HUE_STEP_DEGREES / 2) / HUE_STEP_DEGREES) % HUE_STEP_COUNT;
        editValues_[LED_COLOR_FIELD_WHITE] = SATURATION_MAX - saturation;
        editValues_[LED_COLOR_FIELD_BRIGHTNESS] = loadedChannelValues_[LED_CHANNEL_BRIGHTNESS];
        return;
    }

    for (uint8_t i = 0; i < fieldCount && i < MAX_STORED_ACTIONS; ++i)
    {
        editValues_[i] = loadedChannelValues_[i];
    }
}

const char *const *GuiController::commandsFor(OutputType type, uint8_t &count) const
{
    switch (type)
    {
        case OutputType::Led:
            count = itemCount(LED_COMMANDS);
            return LED_COMMANDS;
        case OutputType::Servo:
            count = itemCount(SERVO_COMMANDS);
            return SERVO_COMMANDS;
        case OutputType::Stepper:
            count = itemCount(STEPPER_COMMANDS);
            return STEPPER_COMMANDS;
        default:
            count = itemCount(RELAY_COMMANDS);
            return RELAY_COMMANDS;
    }
}

const char *const *GuiController::fieldsFor(
    OutputType type, uint8_t commandIndex, uint8_t &count
) const
{
    switch (type)
    {
        case OutputType::Led:
            if (commandIndex == LED_COMMAND_RGB)
            {
                count = itemCount(LED_RGB_FIELDS);
                return LED_RGB_FIELDS;
            }
            count = itemCount(LED_COLOR_FIELDS);
            return LED_COLOR_FIELDS;
        case OutputType::Servo:
            count = itemCount(SERVO_FIELDS);
            return SERVO_FIELDS;
        case OutputType::Stepper:
            count = itemCount(STEPPER_FIELDS);
            return STEPPER_FIELDS;
        default:
            count = 0; // Relay - command is the whole action, no values
            return nullptr;
    }
}

int GuiController::valueMaxForField(OutputType type, uint8_t commandIndex, uint8_t fieldIndex) const
{
    if (type == OutputType::Led && commandIndex == LED_COMMAND_COLOR
        && fieldIndex == LED_COLOR_FIELD_HUE)
    {
        return HUE_STEP_COUNT - 1;
    }

    // ServoOutput clamps to its own configured min/max anyway (see
    // ServoOutput.cpp), but stopping the encoder at 180 keeps Value
    // Entry from scrolling through 75 values that all look identical.
    return type == OutputType::Servo ? 180 : 255;
}

int GuiController::stepForField(OutputType type, uint8_t commandIndex, uint8_t fieldIndex) const
{
    if (type == OutputType::Led && commandIndex == LED_COMMAND_COLOR)
    {
        // Hue is an index into the 24 steps, so one click is one hue.
        // White and Brightness cross their whole range in 32 clicks -
        // fine enough to blend by eye, coarse enough to get end to end.
        return fieldIndex == LED_COLOR_FIELD_HUE ? 1 : 8;
    }

    return 5;
}

bool GuiController::fieldWraps(OutputType type, uint8_t commandIndex, uint8_t fieldIndex) const
{
    // Only hue - it's a wheel, so running off one end should come back on
    // the other. Every other field is a range with real ends.
    return type == OutputType::Led && commandIndex == LED_COMMAND_COLOR
           && fieldIndex == LED_COLOR_FIELD_HUE;
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

bool GuiController::uiActionInfo(
    uint8_t uiIndex, uint8_t &startOut, uint8_t &lengthOut, uint8_t &outputOut
) const
{
    const Action *actions = showEngine_->getActions(currentCueIndex_);
    const uint8_t actionCount = showEngine_->getActionCount(currentCueIndex_);

    if (actions == nullptr)
    {
        return false;
    }

    uint8_t row = 0;
    uint8_t i = 0;

    while (i < actionCount)
    {
        const uint8_t output = outputIndexForChannel(actions[i].outputId);

        // Consecutive actions on the same output are one edited action -
        // an LED color is written as four channel actions in a row (see
        // GuiController.h). A channel no catalog entry claims gets a row
        // of its own rather than being hidden.
        uint8_t end = i + 1;
        if (output != OUTPUT_NOT_FOUND)
        {
            while (end < actionCount && outputIndexForChannel(actions[end].outputId) == output)
            {
                end++;
            }
        }

        if (row == uiIndex)
        {
            startOut = i;
            lengthOut = end - i;
            outputOut = output;
            return true;
        }

        row++;
        i = end;
    }

    return false;
}

uint8_t GuiController::uiActionCount() const
{
    uint8_t row = 0;
    uint8_t start = 0;
    uint8_t length = 0;
    uint8_t output = 0;

    while (uiActionInfo(row, start, length, output))
    {
        row++;
    }

    return row;
}

int32_t GuiController::valueForChannel(
    const Action *actions, uint8_t start, uint8_t length, uint8_t channel
)
{
    for (uint8_t i = 0; i < length; ++i)
    {
        if (actions[start + i].outputId == channel)
        {
            return actions[start + i].value;
        }
    }

    return 0;
}

void GuiController::previewCurrentEdit()
{
    if (outputManager_ == nullptr)
    {
        return;
    }

    // The whole action is sent, not just the field being edited - a color
    // only reads correctly on the strip when R/G/B/Brightness all arrive
    // together. Exactly the actions commitActionFlow() would store.
    //
    // Which command is selected doesn't change what's previewed yet:
    // fields are grouped by output type, not per command (see
    // fieldsFor()), and ActionEngine only implements Level regardless.
    Action preview[MAX_STORED_ACTIONS];
    const uint8_t previewCount = buildActions(outputTypeOf(selectedOutputIndex_), preview);

    if (previewCount == 0)
    {
        return;
    }

    // Goes through ActionEngine rather than OutputManager directly, so
    // editing drives hardware over exactly the same path GO does - see
    // ActionEngine.h on being reusable outside ShowEngine.
    previewEngine_.executeActions(preview, previewCount, *outputManager_);
}

void GuiController::beginRename(RenameTarget target)
{
    renameTarget_ = target;

    labelEditor_->begin(
        target == RenameTarget::Show
            ? showEngine_->getShowName(optionsShowIndex_)
            : showEngine_->getCueNameAt(currentCueIndex_)
    );

    pushScreen(Screen::NameEdit);
}

void GuiController::commitRename()
{
    if (renameTarget_ == RenameTarget::Show)
    {
        showEngine_->renameShow(optionsShowIndex_, labelEditor_->buffer());
    }
    else
    {
        showEngine_->renameCue(currentCueIndex_, labelEditor_->buffer());
    }
}

void GuiController::buildAvailableOutputs()
{
    availableOutputCount_ = 0;

    for (uint8_t output = 0; output < OUTPUT_COUNT && output < MAX_OUTPUTS; ++output)
    {
        bool taken = false;

        uint8_t row = 0;
        uint8_t rowStart = 0;
        uint8_t rowLength = 0;
        uint8_t rowOutput = 0;

        while (uiActionInfo(row, rowStart, rowLength, rowOutput))
        {
            // The row being edited doesn't count against itself, or an
            // existing action could never be re-saved to its own output.
            const bool isRowBeingEdited = editGroupLength_ > 0 && rowStart == editGroupStart_;

            if (rowOutput == output && !isRowBeingEdited)
            {
                taken = true;
                break;
            }

            row++;
        }

        if (!taken)
        {
            availableOutputs_[availableOutputCount_++] = output;
        }
    }
}

void GuiController::beginActionFlow(uint8_t start, uint8_t length)
{
    editGroupStart_ = start;
    editGroupLength_ = length;
    actionFlowReturnDepth_ = stackDepth_;

    if (length > 0)
    {
        const Action *actions = showEngine_->getActions(currentCueIndex_);
        const uint8_t output = outputIndexForChannel(actions[start].outputId);

        if (output != OUTPUT_NOT_FOUND)
        {
            selectedOutputIndex_ = output;
        }

        loadValues(outputTypeOf(selectedOutputIndex_), actions, start, length);
    }
    else
    {
        // selectedOutputIndex_/selectedCommandIndex_ deliberately left
        // as whatever was last used, not reset - "use last-used values as
        // defaults" per the UI rule, so repeated Add Action doesn't force
        // re-selecting from scratch each time.
        hasLoadedValues_ = false;
        editValueCount_ = 0;
    }

    editFieldIndex_ = 0;
    buildAvailableOutputs();

    pushScreen(Screen::OutputSelect);

    // Selection indexes the filtered list, so find where the current
    // output sits in it rather than using its catalog index directly.
    currentSelection() = 0;
    for (uint8_t i = 0; i < availableOutputCount_; ++i)
    {
        if (availableOutputs_[i] == selectedOutputIndex_)
        {
            currentSelection() = i;
            break;
        }
    }
}

void GuiController::commitActionFlow()
{
    // One edited action becomes one stored action per channel - the
    // expansion described in GuiController.h. ActionEngine never sees the
    // grouping, only the flat result.
    Action actions[MAX_STORED_ACTIONS];
    const uint8_t count = buildActions(outputTypeOf(selectedOutputIndex_), actions);

    if (count > 0)
    {
        showEngine_->replaceActions(
            currentCueIndex_, editGroupStart_, editGroupLength_, actions, count
        );
    }

    // Jumps straight back to Action List, discarding the Output Select/
    // Command Select/Value Entry frames pushed during this flow - a plain
    // popScreen() would only remove one at a time.
    stackDepth_ = actionFlowReturnDepth_;

    // Lands on "+ Add Action" again rather than wherever the cursor was.
    // Leaving it alone looks like it stays put but doesn't: adding an
    // action grows the list, so the old index now points at the row the
    // action just created instead of the add entry.
    currentSelection() = uiActionCount();
}

void GuiController::handleRotate(int direction)
{
    if (direction == 0)
    {
        return;
    }

    int step = direction > 0 ? 1 : -1;

    switch (currentScreen())
    {
        case Screen::ShowSelect:
            currentSelection() = wrapIndex(currentSelection(), step, showEngine_->getShowCount());
            break;

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
            currentSelection() = wrapIndex(currentSelection(), step, itemCount(MODE_MENU_ITEMS));
            break;

        case Screen::ShowList:
            currentSelection() = wrapIndex(currentSelection(), step, showEngine_->getShowCount() + 1);
            break;

        case Screen::CueList:
            currentSelection() = wrapIndex(currentSelection(), step, showEngine_->getCueCount() + 1);
            break;

        case Screen::ShowOptions:
            currentSelection() = wrapIndex(currentSelection(), step, SHOW_OPTION_COUNT);
            break;

        case Screen::CueOptions:
            currentSelection() = wrapIndex(currentSelection(), step, CUE_OPTION_COUNT);
            break;

        case Screen::CueFadeEntry:
        {
            const int fadeStep = editFadeTenths_ >= FADE_FINE_LIMIT_TENTHS
                                     ? FADE_COARSE_STEP
                                     : FADE_FINE_STEP;
            const int moved = static_cast<int>(editFadeTenths_) + step * fadeStep;
            editFadeTenths_ = static_cast<uint16_t>(
                constrain(moved, 0, static_cast<int>(ShowEngine::MAX_FADE_TENTHS))
            );
            break;
        }

        case Screen::ActionList:
            // Actions, then "+ Add Action", "+ Add Cue", "Done Programming".
            currentSelection() = wrapIndex(currentSelection(), step, uiActionCount() + 3);
            break;

        case Screen::OutputSelect:
            currentSelection() = wrapIndex(currentSelection(), step, availableOutputCount_);
            break;

        case Screen::CommandSelect:
        {
            uint8_t count = 0;
            commandsFor(outputTypeOf(selectedOutputIndex_), count);
            currentSelection() = wrapIndex(currentSelection(), step, count);
            break;
        }

        case Screen::ActionOptions:
            currentSelection() = wrapIndex(currentSelection(), step, ACTION_OPTION_COUNT);
            break;

        case Screen::ValueEntry:
        {
            const OutputType type = outputTypeOf(selectedOutputIndex_);
            const int fieldMax = valueMaxForField(type, selectedCommandIndex_, editFieldIndex_);
            const int moved = editValues_[editFieldIndex_]
                              + step * stepForField(type, selectedCommandIndex_, editFieldIndex_);

            if (fieldWraps(type, selectedCommandIndex_, editFieldIndex_))
            {
                const int span = fieldMax + 1;
                editValues_[editFieldIndex_] = ((moved % span) + span) % span;
            }
            else
            {
                editValues_[editFieldIndex_] = constrain(moved, 0, fieldMax);
            }
            // Live preview - the operator sees the light/servo land on the
            // value while turning the encoder, rather than only after
            // committing the action and GOing the cue.
            previewCurrentEdit();
            break;
        }

        case Screen::SetupList:
            currentSelection() = wrapIndex(currentSelection(), step, 3);
            break;

        case Screen::ControllerLabelEdit:
        case Screen::NameEdit:
            labelEditor_->stepChar(step);
            break;
    }
}

void GuiController::handlePress()
{
    switch (currentScreen())
    {
        case Screen::ShowSelect:
            if (showEngine_->getShowCount() > 0)
            {
                showEngine_->selectShow(currentSelection());

                // Loading a show puts its opening look on stage: selectShow()
                // leaves cue 1 as the current cue, and running it means the
                // outputs match what the operator sees on the display,
                // rather than whatever the previous show left behind.
                showEngine_->executeCurrentCue(*outputManager_);

                pushScreen(Screen::ShowRun);
            }
            break;

        case Screen::ShowRun:
            // GO - ShowEngine::go() moves the selected cue into current
            // (bracketed) and advances the selection past it, then
            // executeCurrentCue() sends that cue's actions to
            // OutputManager (see ShowEngine.h/Action.h).
            showEngine_->go();
            showEngine_->executeCurrentCue(*outputManager_);
            break;

        case Screen::ModeMenu:
            if (currentSelection() == 0) // Show Mode
            {
                stackDepth_ = 1;
                stack_[0] = { Screen::ShowSelect, 0 };
            }
            else if (currentSelection() == 1) // Program Mode
            {
                stackDepth_ = 1;

                // Opens on "+ New Show", the row after the existing shows.
                stack_[0] = { Screen::ShowList, showEngine_->getShowCount() };
            }
            else // Setup Mode
            {
                stackDepth_ = 1;
                stack_[0] = { Screen::SetupList, 0 };
            }
            break;

        case Screen::ShowList:
        {
            const uint8_t showCount = showEngine_->getShowCount();

            if (currentSelection() == showCount) // "+ New Show"
            {
                if (showEngine_->addShow())
                {
                    showEngine_->selectShow(showCount);
                    currentCueIndex_ = 0;
                    pushScreen(Screen::CueList);
                }
            }
            else
            {
                optionsShowIndex_ = currentSelection();
                pushScreen(Screen::ShowOptions);
            }
            break;
        }

        case Screen::ShowOptions:
        {
            const uint8_t option = currentSelection();

            if (option == SHOW_OPTION_EDIT)
            {
                showEngine_->selectShow(optionsShowIndex_);
                currentCueIndex_ = 0;
                popScreen();
                pushScreen(Screen::CueList);
            }
            else if (option == SHOW_OPTION_RENAME)
            {
                beginRename(RenameTarget::Show);
            }
            else if (option == SHOW_OPTION_COPY)
            {
                showEngine_->copyShow(optionsShowIndex_);
                popScreen();
            }
            else if (option == SHOW_OPTION_DELETE)
            {
                showEngine_->removeShow(optionsShowIndex_);
                popScreen();

                const uint8_t showCount = showEngine_->getShowCount();
                if (currentSelection() > showCount)
                {
                    currentSelection() = showCount;
                }
            }
            break;
        }

        case Screen::CueList:
        {
            const uint8_t cueCount = showEngine_->getCueCount();

            if (currentSelection() == cueCount) // "+ Add Cue"
            {
                if (showEngine_->addCue())
                {
                    currentCueIndex_ = cueCount;
                    pushScreen(Screen::ActionList);

                    // A new cue has no actions, so this is row 0 - set
                    // explicitly anyway, so the rule holds if the entry
                    // order on this screen ever changes.
                    currentSelection() = uiActionCount();
                }
            }
            else
            {
                currentCueIndex_ = currentSelection();
                pushScreen(Screen::CueOptions);
            }
            break;
        }

        case Screen::CueOptions:
        {
            const uint8_t option = currentSelection();

            if (option == CUE_OPTION_EDIT)
            {
                // Replaces the options frame, so Back from the action list
                // returns to the cue list rather than back to here.
                popScreen();
                pushScreen(Screen::ActionList);

                // Opens on "+ Add Action" - the row straight after the
                // existing actions - since adding is what you're usually
                // here to do.
                currentSelection() = uiActionCount();
                break;
            }

            if (option == CUE_OPTION_RENAME)
            {
                beginRename(RenameTarget::Cue);
                break;
            }

            if (option == CUE_OPTION_FADE)
            {
                editFadeTenths_ = showEngine_->getCueFadeTenths(currentCueIndex_);
                pushScreen(Screen::CueFadeEntry);
                break;
            }

            if (option == CUE_OPTION_COPY)
            {
                showEngine_->copyCue(currentCueIndex_);
                popScreen();
                break;
            }

            if (option == CUE_OPTION_DELETE)
            {
                showEngine_->removeCue(currentCueIndex_);
                popScreen();

                const uint8_t cueCount = showEngine_->getCueCount();
                if (currentSelection() > cueCount)
                {
                    currentSelection() = cueCount;
                }
                break;
            }

            const bool up = option == CUE_OPTION_MOVE_UP;
            bool moved = false;

            if (up && currentCueIndex_ > 0)
            {
                moved = showEngine_->swapAdjacentCues(currentCueIndex_ - 1);
                if (moved)
                {
                    currentCueIndex_--;
                }
            }
            else if (!up)
            {
                moved = showEngine_->swapAdjacentCues(currentCueIndex_);
                if (moved)
                {
                    currentCueIndex_++;
                }
            }

            if (moved)
            {
                popScreen();
                currentSelection() = currentCueIndex_;
            }
            break;
        }

        case Screen::ActionList:
        {
            const uint8_t rowCount = uiActionCount();

            if (currentSelection() == rowCount) // "+ Add Action"
            {
                // Appended at the end of the stored list, which is also
                // the order they execute in.
                beginActionFlow(showEngine_->getActionCount(currentCueIndex_), 0);
            }
            else if (currentSelection() == rowCount + 2) // "Done Programming"
            {
                stackDepth_ = 1;
                stack_[0] = { Screen::ModeMenu, 0 };
            }
            else if (currentSelection() == rowCount + 1) // "+ Add Cue"
            {
                // Adds the next cue and moves straight into it, so a show
                // can be built cue after cue without going back out to
                // the cue list between each one.
                const uint8_t cueCount = showEngine_->getCueCount();
                if (showEngine_->addCue())
                {
                    currentCueIndex_ = cueCount;
                    currentSelection() = 0; // new cue is empty - row 0 is "+ Add Action"
                }
            }
            else
            {
                // Remembered now so the options screen (and whichever
                // operation it runs) doesn't have to resolve the row
                // again - Move/Delete change the grouping underneath it.
                uint8_t output = 0;

                if (uiActionInfo(currentSelection(), editGroupStart_, editGroupLength_, output))
                {
                    pushScreen(Screen::ActionOptions);
                }
            }
            break;
        }

        case Screen::ActionOptions:
        {
            const uint8_t option = currentSelection();

            if (option == ACTION_OPTION_EDIT)
            {
                // Drops the options frame first, so committing the edit
                // returns to the Action List rather than back to here.
                popScreen();
                beginActionFlow(editGroupStart_, editGroupLength_);
                break;
            }

            if (option == ACTION_OPTION_DELETE)
            {
                showEngine_->replaceActions(
                    currentCueIndex_, editGroupStart_, editGroupLength_, nullptr, 0
                );
                popScreen();

                // The row this came from may no longer exist - clamp the
                // Action List's selection onto something that does.
                const uint8_t rowCount = uiActionCount();
                if (currentSelection() > rowCount)
                {
                    currentSelection() = rowCount;
                }
                break;
            }

            // Move Up / Move Down - swap with the neighbouring row's whole
            // group, not with a single stored action, since one row can be
            // several actions.
            const bool up = option == ACTION_OPTION_MOVE_UP;
            uint8_t neighbourStart = 0;
            uint8_t neighbourLength = 0;
            uint8_t neighbourOutput = 0;
            uint8_t row = 0;
            uint8_t start = 0;
            uint8_t length = 0;
            uint8_t output = 0;
            bool moved = false;

            // Find which row the remembered group is, then its neighbour.
            while (uiActionInfo(row, start, length, output))
            {
                if (start == editGroupStart_)
                {
                    break;
                }
                row++;
            }

            if (up && row > 0 && uiActionInfo(row - 1, neighbourStart, neighbourLength, neighbourOutput))
            {
                moved = showEngine_->swapAdjacentActions(
                    currentCueIndex_, neighbourStart, neighbourLength, editGroupLength_
                );
                if (moved)
                {
                    editGroupStart_ = neighbourStart;
                }
            }
            else if (!up && uiActionInfo(row + 1, neighbourStart, neighbourLength, neighbourOutput))
            {
                moved = showEngine_->swapAdjacentActions(
                    currentCueIndex_, editGroupStart_, editGroupLength_, neighbourLength
                );
                if (moved)
                {
                    editGroupStart_ += neighbourLength;
                }
            }

            if (moved)
            {
                popScreen();
                currentSelection() = up
                    ? static_cast<uint8_t>(currentSelection() > 0 ? currentSelection() - 1 : 0)
                    : static_cast<uint8_t>(currentSelection() + 1);
            }
            break;
        }

        case Screen::OutputSelect:
        {
            if (currentSelection() >= availableOutputCount_)
            {
                break; // nothing selectable - every output is already used
            }

            selectedOutputIndex_ = availableOutputs_[currentSelection()];

            // Output types don't all offer the same number of commands,
            // and the last-used index is carried over between actions -
            // so it can point past the end of the new output's list.
            uint8_t commandCount = 0;
            commandsFor(outputTypeOf(selectedOutputIndex_), commandCount);
            if (selectedCommandIndex_ >= commandCount)
            {
                selectedCommandIndex_ = 0;
            }

            if (commandCount <= 1)
            {
                // Nothing to choose between - showing a one-item menu
                // would just be a press the operator has to make. Straight
                // to the values instead (the servo's Position).
                selectedCommandIndex_ = 0;
                populateEditFields();

                if (editValueCount_ > 0)
                {
                    pushScreen(Screen::ValueEntry);
                    previewCurrentEdit();
                }
                else
                {
                    commitActionFlow();
                }
                break;
            }

            pushScreen(Screen::CommandSelect);
            currentSelection() = selectedCommandIndex_;
            break;
        }

        case Screen::CommandSelect:
        {
            selectedCommandIndex_ = currentSelection();

            // Fills the dials for this command - the same stored action
            // becomes hue/white/brightness under Color and four raw
            // channels under RGB.
            populateEditFields();

            const uint8_t fieldCount = editValueCount_;

            if (fieldCount == 0)
            {
                // Relay - the command itself is the whole action.
                commitActionFlow();
            }
            else
            {
                pushScreen(Screen::ValueEntry);
                // Show the starting values on the hardware immediately, so
                // the first encoder click isn't the first visible change.
                previewCurrentEdit();
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
                commitActionFlow();
            }
            break;

        case Screen::CueFadeEntry:
            // Confirms the dialled time and steps back to the cue's option
            // menu, which shows the new value on its Fade Time row.
            showEngine_->setCueFadeTenths(currentCueIndex_, editFadeTenths_);
            popScreen();
            break;

        case Screen::SetupList:
            if (currentSelection() == 0) // Controller Label
            {
                labelEditor_->begin(deviceInfo_->unitLabel);
                pushScreen(Screen::ControllerLabelEdit);
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

        case Screen::ControllerLabelEdit:
            if (labelEditor_->confirmChar())
            {
                if (commitUnitLabel_ != nullptr)
                {
                    commitUnitLabel_();
                }
                popScreen();
            }
            break;

        case Screen::NameEdit:
            if (labelEditor_->confirmChar())
            {
                commitRename();
                popScreen();
            }
            break;
    }
}

void GuiController::handleBack()
{
    switch (currentScreen())
    {
        case Screen::ShowSelect:
        case Screen::ShowList:
        case Screen::SetupList:
            // Root screen for the current mode - nothing left to pop
            // within the mode itself, so Back goes up to the mode
            // switcher instead of doing nothing.
            pushScreen(Screen::ModeMenu);
            break;

        case Screen::ShowRun:
            // Reached either by picking a show (so Back returns to the
            // picker) or by booting straight into it, where it *is* the
            // root and there's nothing to pop - without this second case
            // Back would silently do nothing and strand the operator.
            if (stackDepth_ > 1)
            {
                popScreen();
            }
            else
            {
                pushScreen(Screen::ModeMenu);
            }
            break;

        case Screen::ModeMenu:
            // Top of the navigation hierarchy - Back does nothing here,
            // it doesn't cycle back down into whichever mode was
            // showing before.
            break;

        case Screen::ControllerLabelEdit:
        case Screen::NameEdit:
            labelEditor_->back();
            if (!labelEditor_->isActive())
            {
                popScreen();
            }
            break;

        case Screen::ValueEntry:
            // Steps back through the dials one at a time (BRIGHTNESS ->
            // WHITE -> HUE) rather than abandoning the whole action on the
            // first press - only backing out of the first field leaves.
            if (editFieldIndex_ > 0)
            {
                editFieldIndex_--;
                previewCurrentEdit();
            }
            else
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
        case Screen::ShowSelect:
        {
            const uint8_t showCount = showEngine_->getShowCount();

            if (showCount == 0)
            {
                itemPointers_[0] = NO_SHOWS_ITEM;
                Display::showGuiList("SELECT SHOW", nullptr, itemPointers_, 1, 0);
                break;
            }

            for (uint8_t i = 0; i < showCount && i < MAX_LIST_ITEMS; ++i)
            {
                itemPointers_[i] = showEngine_->getShowName(i);
            }

            Display::showGuiList("SELECT SHOW", nullptr, itemPointers_, showCount, currentSelection());
            break;
        }

        case Screen::ShowRun:
        {
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
            Display::showGuiList("MODE", nullptr, MODE_MENU_ITEMS, itemCount(MODE_MENU_ITEMS), currentSelection());
            break;

        case Screen::ShowList:
        {
            const uint8_t showCount = showEngine_->getShowCount();
            uint8_t count = 0;

            for (uint8_t i = 0; i < showCount && count < MAX_LIST_ITEMS - 1; ++i)
            {
                itemPointers_[count++] = showEngine_->getShowName(i);
            }

            itemPointers_[count++] = "+ New Show";
            Display::showGuiList("PROGRAM SHOW", nullptr, itemPointers_, count, currentSelection());
            break;
        }

        case Screen::CueList:
        {
            const uint8_t cueCount = showEngine_->getCueCount();
            uint8_t count = 0;

            for (uint8_t i = 0; i < cueCount && count < MAX_LIST_ITEMS - 1; ++i)
            {
                // The Q number is the position, generated here rather
                // than stored - see ShowEngine.h. The fade time sits
                // before the name, not after: a row is 23 characters at
                // this font, so a long name has to be what gets cut off,
                // never the timing.
                const uint16_t fadeTenths = showEngine_->getCueFadeTenths(i);
                snprintf(
                    listLabels_[count], LABEL_SIZE, "Q%02u %u.%us %s",
                    static_cast<unsigned>(i + 1),
                    static_cast<unsigned>(fadeTenths / 10),
                    static_cast<unsigned>(fadeTenths % 10),
                    showEngine_->getCueNameAt(i)
                );
                itemPointers_[count] = listLabels_[count];
                count++;
            }

            itemPointers_[count++] = "+ Add Cue";

            Display::showGuiList(
                "CUES", showEngine_->getShowName(), itemPointers_, count, currentSelection()
            );
            break;
        }

        case Screen::ActionList:
        {
            const Action *actions = showEngine_->getActions(currentCueIndex_);
            uint8_t count = 0;
            uint8_t row = 0;
            uint8_t start = 0;
            uint8_t length = 0;
            uint8_t output = 0;

            // Leaves room for both trailing entries appended below.
            while (count < MAX_LIST_ITEMS - MAX_TRAILING_ITEMS
                   && uiActionInfo(row, start, length, output))
            {
                if (output != OUTPUT_NOT_FOUND && outputTypeOf(output) == OutputType::Led)
                {
                    // Hue in degrees plus brightness - the color as the
                    // operator thinks of it, rather than the raw r/g/b
                    // channels underneath. Degrees rather than the step
                    // index so the number means something on its own.
                    int hueDegrees = 0;
                    int saturation = 0;
                    rgbToHueSaturation(
                        valueForChannel(actions, start, length, LED_CHANNELS[LED_CHANNEL_RED]),
                        valueForChannel(actions, start, length, LED_CHANNELS[LED_CHANNEL_GREEN]),
                        valueForChannel(actions, start, length, LED_CHANNELS[LED_CHANNEL_BLUE]),
                        hueDegrees, saturation
                    );

                    snprintf(
                        listLabels_[count], LABEL_SIZE, "LED %dd B%d",
                        hueDegrees,
                        static_cast<int>(
                            valueForChannel(actions, start, length, LED_CHANNELS[LED_CHANNEL_BRIGHTNESS])
                        )
                    );
                }
                else if (output != OUTPUT_NOT_FOUND)
                {
                    uint8_t channelCount = 0;
                    const uint8_t *channels = channelsFor(outputTypeOf(output), channelCount);
                    snprintf(
                        listLabels_[count], LABEL_SIZE, "%s %d",
                        OUTPUT_LABELS[output],
                        static_cast<int>(
                            channels == nullptr
                                ? actions[start].value
                                : valueForChannel(actions, start, length, channels[0])
                        )
                    );
                }
                else
                {
                    // A channel the catalog doesn't claim - shown by raw
                    // channel number rather than hidden, so a stored
                    // action never silently disappears from the list.
                    snprintf(
                        listLabels_[count], LABEL_SIZE, "CH%u %d",
                        static_cast<unsigned>(actions[start].outputId),
                        static_cast<int>(actions[start].value)
                    );
                }

                itemPointers_[count] = listLabels_[count];
                count++;
                row++;
            }

            itemPointers_[count++] = "+ Add Action";
            itemPointers_[count++] = "+ Add Cue";
            itemPointers_[count++] = DONE_PROGRAMMING_ITEM;
            Display::showGuiList(
                "ACTIONS", showEngine_->getCueNameAt(currentCueIndex_),
                itemPointers_, count, currentSelection()
            );
            break;
        }

        case Screen::ShowOptions:
            Display::showGuiList(
                "SHOW", showEngine_->getShowName(optionsShowIndex_),
                SHOW_OPTIONS, SHOW_OPTION_COUNT, currentSelection()
            );
            break;

        case Screen::CueOptions:
        {
            for (uint8_t i = 0; i < CUE_OPTION_COUNT && i < MAX_LIST_ITEMS; ++i)
            {
                itemPointers_[i] = CUE_OPTIONS[i];
            }

            // Fade Time carries its current value, so the cue's timing is
            // readable without opening the screen to check it.
            const uint16_t fadeTenths = showEngine_->getCueFadeTenths(currentCueIndex_);
            snprintf(
                listLabels_[CUE_OPTION_FADE], LABEL_SIZE, "%s %u.%us",
                CUE_OPTIONS[CUE_OPTION_FADE],
                static_cast<unsigned>(fadeTenths / 10),
                static_cast<unsigned>(fadeTenths % 10)
            );
            itemPointers_[CUE_OPTION_FADE] = listLabels_[CUE_OPTION_FADE];

            Display::showGuiList(
                "CUE", showEngine_->getCueNameAt(currentCueIndex_),
                itemPointers_, CUE_OPTION_COUNT, currentSelection()
            );
            break;
        }

        case Screen::CueFadeEntry:
        {
            char valueText[12];
            snprintf(
                valueText, sizeof(valueText), "%u.%u sec",
                static_cast<unsigned>(editFadeTenths_ / 10),
                static_cast<unsigned>(editFadeTenths_ % 10)
            );
            Display::showGuiValueEntry(
                "CUE FADE", showEngine_->getCueNameAt(currentCueIndex_),
                "TIME", valueText, 0, 1
            );
            break;
        }

        case Screen::ActionOptions:
            Display::showGuiList(
                "ACTION", showEngine_->getCueNameAt(currentCueIndex_),
                ACTION_OPTIONS, ACTION_OPTION_COUNT, currentSelection()
            );
            break;

        case Screen::OutputSelect:
        {
            if (availableOutputCount_ == 0)
            {
                // Every output already has an action in this cue - one per
                // output per cue is the rule, so there is nothing to add.
                itemPointers_[0] = "(all outputs used)";
                Display::showGuiList("OUTPUT", nullptr, itemPointers_, 1, 0);
                break;
            }

            for (uint8_t i = 0; i < availableOutputCount_; ++i)
            {
                itemPointers_[i] = OUTPUT_LABELS[availableOutputs_[i]];
            }

            Display::showGuiList(
                "OUTPUT", nullptr, itemPointers_, availableOutputCount_, currentSelection()
            );
            break;
        }

        case Screen::CommandSelect:
        {
            uint8_t count = 0;
            const char *const *commands = commandsFor(outputTypeOf(selectedOutputIndex_), count);
            Display::showGuiList(
                "COMMAND", OUTPUT_LABELS[selectedOutputIndex_], commands, count, currentSelection()
            );
            break;
        }

        case Screen::ValueEntry:
        {
            const OutputType type = outputTypeOf(selectedOutputIndex_);

            uint8_t fieldCount = 0;
            const char *const *fields = fieldsFor(type, selectedCommandIndex_, fieldCount);

            uint8_t cmdCount = 0;
            const char *const *commands = commandsFor(type, cmdCount);

            char context[24];
            snprintf(
                context, sizeof(context), "%s/%s",
                OUTPUT_LABELS[selectedOutputIndex_], commands[selectedCommandIndex_]
            );

            // Hue reads in degrees rather than as its step index - 0/120/
            // 240 are recognisable as red/green/blue, 7 of 23 isn't.
            char valueText[8];
            if (type == OutputType::Led && selectedCommandIndex_ == LED_COMMAND_COLOR
                && editFieldIndex_ == LED_COLOR_FIELD_HUE)
            {
                snprintf(
                    valueText, sizeof(valueText), "%d",
                    editValues_[editFieldIndex_] * HUE_STEP_DEGREES
                );
            }
            else
            {
                snprintf(valueText, sizeof(valueText), "%d", editValues_[editFieldIndex_]);
            }

            Display::showGuiValueEntry(
                "EDIT ACTION", context, fields[editFieldIndex_], valueText, editFieldIndex_, fieldCount
            );
            break;
        }

        case Screen::SetupList:
            Display::showGuiList("SETUP", nullptr, SETUP_ITEMS, itemCount(SETUP_ITEMS), currentSelection());
            break;

        case Screen::ControllerLabelEdit:
        case Screen::NameEdit:
            Display::showUnitLabelEdit(labelEditor_->buffer(), labelEditor_->cursor(), true);
            break;
    }
}
