#include "ShowEngine.h"

#include <Arduino.h>
#include <cstdio>

namespace
{
    // OutputManager channel numbers the test show's actions target -
    // must match RX main.cpp's OUTPUT_CHANNEL_* constants (that file's
    // anonymous namespace isn't reachable from here, so this is kept in
    // sync by hand for this first version).
    constexpr uint8_t TEST_SHOW_SERVO_OUTPUT_ID = 1;
}

void ShowEngine::begin()
{
    loadTestShow();

    Serial.println("SHOW ENGINE READY");
    Serial.println();
    Serial.println("SHOW:");
    Serial.println(showName_);
    Serial.println();
    Serial.println("CURRENT CUE:");
    Serial.println(currentCue_);
    Serial.println(getCueName(currentCue_));
    Serial.println();
    Serial.println("SELECTED CUE:");
    Serial.println(selectedCue_);
    Serial.println(getCueName(selectedCue_));
}

void ShowEngine::setCue(uint8_t index, uint8_t number, const char *name)
{
    if (index >= MAX_CUES)
    {
        return;
    }

    cues_[index].number = number;
    snprintf(cues_[index].name, CUE_NAME_SIZE, "%s", name);
    cues_[index].actionCount = 0;
}

void ShowEngine::addAction(uint8_t cueIndex, uint8_t outputId, ActionCommand command, int32_t value)
{
    if (cueIndex >= MAX_CUES)
    {
        return;
    }

    Cue &cue = cues_[cueIndex];
    if (cue.actionCount >= MAX_ACTIONS_PER_CUE)
    {
        return;
    }

    cue.actions[cue.actionCount].outputId = outputId;
    cue.actions[cue.actionCount].command = command;
    cue.actions[cue.actionCount].value = value;
    cue.actionCount++;
}

void ShowEngine::loadTestShow()
{
    snprintf(showName_, SHOW_NAME_SIZE, "Dragon Battle");

    cueCount_ = 3;

    setCue(0, 1, "Home Position");
    addAction(0, TEST_SHOW_SERVO_OUTPUT_ID, ActionCommand::Level, 0);

    setCue(1, 2, "Wake Up");
    addAction(1, TEST_SHOW_SERVO_OUTPUT_ID, ActionCommand::Level, 90);

    setCue(2, 3, "Roar");
    addAction(2, TEST_SHOW_SERVO_OUTPUT_ID, ActionCommand::Level, 180);

    currentCue_ = 1;
    selectedCue_ = currentCue_ + 1;
}

void ShowEngine::go()
{
    currentCue_ = selectedCue_;
    selectedCue_ = currentCue_ + 1;
}

void ShowEngine::home()
{
    selectedCue_ = currentCue_ + 1;
}

void ShowEngine::nextCue()
{
    selectedCue_++;
}

void ShowEngine::previousCue()
{
    if (selectedCue_ > 1)
    {
        selectedCue_--;
    }
}

const char *ShowEngine::getShowName() const
{
    return showName_;
}

uint8_t ShowEngine::getCurrentCue() const
{
    return currentCue_;
}

uint8_t ShowEngine::getSelectedCue() const
{
    return selectedCue_;
}

uint8_t ShowEngine::getCueCount() const
{
    return cueCount_;
}

const char *ShowEngine::getCueName(uint8_t cueNumber) const
{
    for (uint8_t i = 0; i < cueCount_; ++i)
    {
        if (cues_[i].number == cueNumber)
        {
            return cues_[i].name;
        }
    }

    return "";
}

void ShowEngine::executeCurrentCue(StageLink::OutputManager &outputManager)
{
    for (uint8_t i = 0; i < cueCount_; ++i)
    {
        if (cues_[i].number != currentCue_)
        {
            continue;
        }

        const Cue &cue = cues_[i];
        for (uint8_t a = 0; a < cue.actionCount; ++a)
        {
            const Action &action = cue.actions[a];

            if (action.command == ActionCommand::Level)
            {
                outputManager.update(action.outputId, action.value);
            }
        }

        return;
    }
}
