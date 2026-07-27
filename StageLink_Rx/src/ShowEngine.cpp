#include "ShowEngine.h"

#include <Arduino.h>
#include <cstdio>
#include "ShowStorage.h"

namespace
{
    // OutputManager channel numbers the test show's actions target -
    // must match RX main.cpp's OUTPUT_CHANNEL_* constants (that file's
    // anonymous namespace isn't reachable from here, so this is kept in
    // sync by hand for this first version).
    constexpr uint8_t TEST_SHOW_SERVO_OUTPUT_ID = 1;
    constexpr uint8_t TEST_SHOW_LED_BRIGHTNESS_OUTPUT_ID = 2;
    constexpr uint8_t TEST_SHOW_LED_RED_OUTPUT_ID = 3;
    constexpr uint8_t TEST_SHOW_LED_GREEN_OUTPUT_ID = 4;
    constexpr uint8_t TEST_SHOW_LED_BLUE_OUTPUT_ID = 5;
}

void ShowEngine::begin()
{
    // Starts from whatever is in flash. Nothing stored - a fresh unit, or
    // firmware whose layout changed - simply leaves the list empty; a unit
    // ships with no shows and the operator creates them in Program Mode.
    // Seeding a demo show is main.cpp's explicit loadTestShow() call
    // (behind LOAD_TEST_SHOW_ON_BOOT), never something begin() does.
    showCount_ = 0;
    selectedShowIndex_ = 0;
    currentCue_ = 1;
    selectedCue_ = currentCue_ + 1;

    StageLink::ShowStorage::load(shows_, sizeof(Show), MAX_SHOWS, showCount_);

    // Ids have to resume past everything already stored, or a new show
    // could be created carrying an id a loaded one is already using.
    for (uint8_t i = 0; i < showCount_; ++i)
    {
        if (shows_[i].id >= nextShowId_)
        {
            nextShowId_ = shows_[i].id + 1;
        }

        for (uint8_t c = 0; c < shows_[i].cueCount; ++c)
        {
            if (shows_[i].cues[c].id >= nextCueId_)
            {
                nextCueId_ = shows_[i].cues[c].id + 1;
            }
        }
    }

    dirty_ = false;

    Serial.println("SHOW ENGINE READY");
    Serial.println();
    Serial.println("SHOW:");
    Serial.println(getShowName());
    Serial.println();
    Serial.println("CURRENT CUE:");
    Serial.println(currentCue_);
    Serial.println(getCueName(currentCue_));
    Serial.println();
    Serial.println("SELECTED CUE:");
    Serial.println(selectedCue_);
    Serial.println(getCueName(selectedCue_));
}

void ShowEngine::markDirty()
{
    dirty_ = true;
    lastChangeMs_ = millis();
}

bool ShowEngine::hasUnsavedChanges() const
{
    return dirty_;
}

bool ShowEngine::save()
{
    const bool saved = StageLink::ShowStorage::save(shows_, sizeof(Show), showCount_);

    // Cleared even on a failed write: retrying the same bad write every
    // loop would hammer the flash without ever succeeding. The edits are
    // still live in RAM, and the next edit marks it dirty again.
    dirty_ = false;

    return saved;
}

void ShowEngine::tick()
{
    if (!dirty_)
    {
        return;
    }

    if (millis() - lastChangeMs_ < AUTOSAVE_QUIET_MS)
    {
        return;
    }

    save();
}

ShowEngine::Show *ShowEngine::selectedShow()
{
    if (selectedShowIndex_ >= showCount_)
    {
        return nullptr;
    }

    return &shows_[selectedShowIndex_];
}

const ShowEngine::Show *ShowEngine::selectedShow() const
{
    if (selectedShowIndex_ >= showCount_)
    {
        return nullptr;
    }

    return &shows_[selectedShowIndex_];
}

ShowEngine::Cue *ShowEngine::cueAt(uint8_t cueIndex)
{
    Show *show = selectedShow();
    if (show == nullptr || cueIndex >= show->cueCount)
    {
        return nullptr;
    }

    return &show->cues[cueIndex];
}

const ShowEngine::Cue *ShowEngine::cueAt(uint8_t cueIndex) const
{
    const Show *show = selectedShow();
    if (show == nullptr || cueIndex >= show->cueCount)
    {
        return nullptr;
    }

    return &show->cues[cueIndex];
}

void ShowEngine::setCue(uint8_t showIndex, uint8_t index, uint8_t number, const char *name)
{
    if (showIndex >= MAX_SHOWS || index >= MAX_CUES)
    {
        return;
    }

    Cue &cue = shows_[showIndex].cues[index];
    cue.id = nextCueId_++;
    snprintf(cue.name, CUE_NAME_SIZE, "%s", name);
    cue.autoName = false; // the test show's cues are deliberately named
    cue.actionCount = 0;
    (void)number;         // position is the number now - see ShowEngine.h
}

void ShowEngine::addTestAction(
    uint8_t showIndex, uint8_t cueIndex, uint8_t outputId, ActionCommand command, int32_t value
)
{
    if (showIndex >= MAX_SHOWS || cueIndex >= MAX_CUES)
    {
        return;
    }

    Cue &cue = shows_[showIndex].cues[cueIndex];
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
    showCount_ = 0;

    // TEST DATA - not what a unit ships with. Each cue drives both real outputs (servo +
    // LED) so a single GO exercises the whole multi-action path:
    // ShowEngine hands the entire action list to ActionEngine, which
    // applies every entry in order. The LED's red/green/blue all default
    // to 255 (see LEDOutput.h), so a cue that wants a specific color has
    // to set all three - it can't just raise the one it cares about.
    shows_[showCount_].id = nextShowId_++;
    shows_[showCount_].autoName = false;
    snprintf(shows_[showCount_].name, SHOW_NAME_SIZE, "Dragon Battle");
    shows_[showCount_].cueCount = 3;

    setCue(showCount_, 0, 1, "Home Position");
    addTestAction(showCount_, 0, TEST_SHOW_SERVO_OUTPUT_ID, ActionCommand::Level, 0);
    addTestAction(showCount_, 0, TEST_SHOW_LED_BRIGHTNESS_OUTPUT_ID, ActionCommand::Level, 0);

    setCue(showCount_, 1, 2, "Wake Up");
    addTestAction(showCount_, 1, TEST_SHOW_SERVO_OUTPUT_ID, ActionCommand::Level, 90);
    addTestAction(showCount_, 1, TEST_SHOW_LED_RED_OUTPUT_ID, ActionCommand::Level, 0);
    addTestAction(showCount_, 1, TEST_SHOW_LED_GREEN_OUTPUT_ID, ActionCommand::Level, 255);
    addTestAction(showCount_, 1, TEST_SHOW_LED_BLUE_OUTPUT_ID, ActionCommand::Level, 0);
    addTestAction(showCount_, 1, TEST_SHOW_LED_BRIGHTNESS_OUTPUT_ID, ActionCommand::Level, 120);

    setCue(showCount_, 2, 3, "Roar");
    addTestAction(showCount_, 2, TEST_SHOW_SERVO_OUTPUT_ID, ActionCommand::Level, 180);
    addTestAction(showCount_, 2, TEST_SHOW_LED_RED_OUTPUT_ID, ActionCommand::Level, 255);
    addTestAction(showCount_, 2, TEST_SHOW_LED_GREEN_OUTPUT_ID, ActionCommand::Level, 0);
    addTestAction(showCount_, 2, TEST_SHOW_LED_BLUE_OUTPUT_ID, ActionCommand::Level, 0);
    addTestAction(showCount_, 2, TEST_SHOW_LED_BRIGHTNESS_OUTPUT_ID, ActionCommand::Level, 255);

    showCount_++;

    selectedShowIndex_ = 0;
    currentCue_ = 1;
    selectedCue_ = currentCue_ + 1;

    markDirty();
}

uint8_t ShowEngine::getShowCount() const
{
    return showCount_;
}

uint8_t ShowEngine::getSelectedShowIndex() const
{
    return selectedShowIndex_;
}

void ShowEngine::selectShow(uint8_t showIndex)
{
    if (showIndex >= showCount_)
    {
        return;
    }

    selectedShowIndex_ = showIndex;

    // Same "start of show" state loadTestShow() leaves behind - without
    // this, a cue pointer from the previously selected show would carry
    // over into a show that may not even have that many cues.
    currentCue_ = 1;
    selectedCue_ = currentCue_ + 1;
}

bool ShowEngine::addShow()
{
    if (showCount_ >= MAX_SHOWS)
    {
        return false;
    }

    shows_[showCount_].id = nextShowId_++;
    shows_[showCount_].autoName = true;
    shows_[showCount_].cueCount = 0;
    showCount_++;

    refreshShowAutoNames();

    markDirty();

    return true;
}

bool ShowEngine::renameShow(uint8_t showIndex, const char *name)
{
    if (showIndex >= showCount_ || name == nullptr)
    {
        return false;
    }

    snprintf(shows_[showIndex].name, SHOW_NAME_SIZE, "%s", name);
    shows_[showIndex].autoName = false;

    markDirty();

    return true;
}

bool ShowEngine::copyShow(uint8_t showIndex)
{
    if (showIndex >= showCount_ || showCount_ >= MAX_SHOWS)
    {
        return false;
    }

    Show &copy = shows_[showCount_];
    copy = shows_[showIndex];

    // Fresh identity for the show and for every cue inside it - a copy
    // must share no ids with the original.
    copy.id = nextShowId_++;
    copy.autoName = false;
    snprintf(copy.name, SHOW_NAME_SIZE, "%s Copy", shows_[showIndex].name);

    for (uint8_t i = 0; i < copy.cueCount; ++i)
    {
        copy.cues[i].id = nextCueId_++;
    }

    showCount_++;

    markDirty();

    return true;
}

bool ShowEngine::removeShow(uint8_t showIndex)
{
    if (showIndex >= showCount_)
    {
        return false;
    }

    for (uint8_t i = showIndex; i + 1 < showCount_; ++i)
    {
        shows_[i] = shows_[i + 1];
    }

    showCount_--;
    refreshShowAutoNames();

    if (selectedShowIndex_ >= showCount_)
    {
        selectedShowIndex_ = showCount_ == 0 ? 0 : showCount_ - 1;
    }

    currentCue_ = clampCueNumber(currentCue_);
    selectedCue_ = clampCueNumber(selectedCue_);

    markDirty();

    return true;
}

uint8_t ShowEngine::lastCueNumber() const
{
    const Show *show = selectedShow();
    if (show == nullptr || show->cueCount == 0)
    {
        return 0;
    }

    // Numbering is positional, so the last cue's number is the count.
    return show->cueCount;
}

uint8_t ShowEngine::clampCueNumber(uint8_t cueNumber) const
{
    const uint8_t last = lastCueNumber();
    if (last == 0)
    {
        return 1; // No cues - nothing to point at, so stay at the start.
    }

    if (cueNumber < 1)
    {
        return 1;
    }

    return cueNumber > last ? last : cueNumber;
}

void ShowEngine::go()
{
    currentCue_ = selectedCue_;

    // Stops on the last programmed cue rather than running off the end -
    // there's nothing beyond it to select, and a selection pointing past
    // the show would display as a blank cue.
    selectedCue_ = clampCueNumber(currentCue_ + 1);
}

void ShowEngine::home()
{
    selectedCue_ = clampCueNumber(currentCue_ + 1);
}

void ShowEngine::nextCue()
{
    selectedCue_ = clampCueNumber(selectedCue_ + 1);
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
    return getShowName(selectedShowIndex_);
}

const char *ShowEngine::getShowName(uint8_t showIndex) const
{
    if (showIndex >= showCount_)
    {
        return "";
    }

    return shows_[showIndex].name;
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
    const Show *show = selectedShow();
    return show == nullptr ? 0 : show->cueCount;
}

const char *ShowEngine::getCueName(uint8_t cueNumber) const
{
    const Show *show = selectedShow();
    if (show == nullptr)
    {
        return "";
    }

    // Cue numbers are positions, so this is a direct index rather than a
    // search - see the Cue struct in ShowEngine.h.
    if (cueNumber < 1 || cueNumber > show->cueCount)
    {
        return "";
    }

    return show->cues[cueNumber - 1].name;
}

const char *ShowEngine::getCueNameAt(uint8_t cueIndex) const
{
    const Cue *cue = cueAt(cueIndex);
    return cue == nullptr ? "" : cue->name;
}

bool ShowEngine::addCue()
{
    Show *show = selectedShow();
    if (show == nullptr || show->cueCount >= MAX_CUES)
    {
        return false;
    }

    Cue &cue = show->cues[show->cueCount];
    cue.id = nextCueId_++;
    cue.autoName = true;
    cue.actionCount = 0;
    show->cueCount++;

    refreshCueAutoNames();

    markDirty();

    return true;
}

bool ShowEngine::renameCue(uint8_t cueIndex, const char *name)
{
    Cue *cue = cueAt(cueIndex);
    if (cue == nullptr || name == nullptr)
    {
        return false;
    }

    snprintf(cue->name, CUE_NAME_SIZE, "%s", name);

    // From here the name is the operator's, so moving the cue must not
    // rewrite it - see refreshCueAutoNames().
    cue->autoName = false;

    markDirty();

    return true;
}

bool ShowEngine::copyCue(uint8_t cueIndex)
{
    Show *show = selectedShow();
    if (show == nullptr || cueIndex >= show->cueCount || show->cueCount >= MAX_CUES)
    {
        return false;
    }

    // Inserted directly below the original so the copy runs next.
    for (uint8_t i = show->cueCount; i > cueIndex + 1; --i)
    {
        show->cues[i] = show->cues[i - 1];
    }

    Cue &copy = show->cues[cueIndex + 1];
    copy = show->cues[cueIndex];

    // Fresh identity - the copy is a new object, not another reference to
    // the original.
    copy.id = nextCueId_++;
    copy.autoName = false;
    snprintf(copy.name, CUE_NAME_SIZE, "%s Copy", show->cues[cueIndex].name);

    show->cueCount++;
    refreshCueAutoNames();

    markDirty();

    return true;
}

void ShowEngine::refreshCueAutoNames()
{
    Show *show = selectedShow();
    if (show == nullptr)
    {
        return;
    }

    // Execution numbers need no maintenance - they're positions. Only the
    // generated names have to follow, and only for cues the operator
    // hasn't named themselves.
    for (uint8_t i = 0; i < show->cueCount; ++i)
    {
        if (show->cues[i].autoName)
        {
            snprintf(show->cues[i].name, CUE_NAME_SIZE, "Cue %02u", static_cast<unsigned>(i + 1));
        }
    }
}

void ShowEngine::refreshShowAutoNames()
{
    for (uint8_t i = 0; i < showCount_; ++i)
    {
        if (shows_[i].autoName)
        {
            snprintf(shows_[i].name, SHOW_NAME_SIZE, "Show %02u", static_cast<unsigned>(i + 1));
        }
    }
}

bool ShowEngine::removeCue(uint8_t cueIndex)
{
    Show *show = selectedShow();
    if (show == nullptr || cueIndex >= show->cueCount)
    {
        return false;
    }

    for (uint8_t i = cueIndex; i + 1 < show->cueCount; ++i)
    {
        show->cues[i] = show->cues[i + 1];
    }

    show->cueCount--;
    refreshCueAutoNames();

    // The removed cue may have been the one playing or selected - pull
    // both back inside what's left rather than leaving them pointing at a
    // cue number that no longer exists.
    currentCue_ = clampCueNumber(currentCue_);
    selectedCue_ = clampCueNumber(selectedCue_);

    markDirty();

    return true;
}

bool ShowEngine::swapAdjacentCues(uint8_t firstIndex)
{
    Show *show = selectedShow();
    if (show == nullptr || firstIndex + 1 >= show->cueCount)
    {
        return false;
    }

    const Cue moved = show->cues[firstIndex];
    show->cues[firstIndex] = show->cues[firstIndex + 1];
    show->cues[firstIndex + 1] = moved;

    // Numbers follow position rather than travelling with the cue, so the
    // list still reads 1, 2, 3 top to bottom after a move.
    refreshCueAutoNames();

    markDirty();

    return true;
}

uint8_t ShowEngine::getActionCount(uint8_t cueIndex) const
{
    const Cue *cue = cueAt(cueIndex);
    return cue == nullptr ? 0 : cue->actionCount;
}

const Action *ShowEngine::getActions(uint8_t cueIndex) const
{
    const Cue *cue = cueAt(cueIndex);
    return cue == nullptr ? nullptr : cue->actions;
}

bool ShowEngine::replaceActions(
    uint8_t cueIndex,
    uint8_t startIndex,
    uint8_t removeCount,
    const Action *actions,
    uint8_t count
)
{
    Cue *cue = cueAt(cueIndex);
    if (cue == nullptr || startIndex > cue->actionCount)
    {
        return false;
    }

    if (startIndex + removeCount > cue->actionCount)
    {
        return false;
    }

    const uint8_t tailStart = startIndex + removeCount;
    const uint8_t tailCount = cue->actionCount - tailStart;
    const uint8_t newCount = startIndex + count + tailCount;

    if (newCount > MAX_ACTIONS_PER_CUE)
    {
        return false;
    }

    // Shift the tail into place before writing the replacement, so a
    // grow and a shrink both work with one code path. Copied through a
    // scratch buffer because the ranges can overlap.
    Action tail[MAX_ACTIONS_PER_CUE];
    for (uint8_t i = 0; i < tailCount; ++i)
    {
        tail[i] = cue->actions[tailStart + i];
    }

    for (uint8_t i = 0; i < count; ++i)
    {
        cue->actions[startIndex + i] = actions[i];
    }

    for (uint8_t i = 0; i < tailCount; ++i)
    {
        cue->actions[startIndex + count + i] = tail[i];
    }

    cue->actionCount = newCount;

    markDirty();

    return true;
}

bool ShowEngine::swapAdjacentActions(
    uint8_t cueIndex, uint8_t firstStart, uint8_t firstLength, uint8_t secondLength
)
{
    Cue *cue = cueAt(cueIndex);
    if (cue == nullptr || firstLength == 0 || secondLength == 0)
    {
        return false;
    }

    const uint8_t total = firstLength + secondLength;
    if (firstStart + total > cue->actionCount)
    {
        return false;
    }

    // Built in a scratch buffer first - the two ranges are adjacent, so
    // writing in place would overwrite the second block while reading it.
    Action reordered[MAX_ACTIONS_PER_CUE];
    uint8_t n = 0;

    for (uint8_t i = 0; i < secondLength; ++i)
    {
        reordered[n++] = cue->actions[firstStart + firstLength + i];
    }

    for (uint8_t i = 0; i < firstLength; ++i)
    {
        reordered[n++] = cue->actions[firstStart + i];
    }

    for (uint8_t i = 0; i < total; ++i)
    {
        cue->actions[firstStart + i] = reordered[i];
    }

    markDirty();

    return true;
}

void ShowEngine::executeCurrentCue(StageLink::OutputManager &outputManager)
{
    const Show *show = selectedShow();
    if (show == nullptr)
    {
        return;
    }

    if (currentCue_ >= 1 && currentCue_ <= show->cueCount)
    {
        executeCue(currentCue_ - 1, outputManager);
    }
}

void ShowEngine::executeCue(uint8_t cueIndex, StageLink::OutputManager &outputManager)
{
    const Cue *cue = cueAt(cueIndex);
    if (cue == nullptr)
    {
        return;
    }

    actionEngine_.executeActions(cue->actions, cue->actionCount, outputManager);
}
