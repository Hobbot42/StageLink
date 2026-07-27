// StageLink LabelEditor
// Generic encoder-driven fixed-size label text editor: rotate changes
// the character at the cursor, a (short) press confirms it and advances,
// going back steps to re-edit the previous character - or, at
// the first character, cancels the whole edit. Reuses the project's
// caller's own "go back one step" gesture - the GUI's dedicated Back
// button, or the legacy diagnostic pages' button hold (see RX main.cpp's
// LOCAL_BUTTON_HOLD_MS) - rather than requiring a second
// physical control, since RX's local encoder only has one button.
//
// Purely an in-memory buffer + cursor state machine - it knows nothing
// about persistence (see RX main.cpp's saveUnitLabel) or rendering (see
// Display::showUnitLabelEdit). That's what makes it reusable for
// whichever label a caller is editing (today: the unit label; later:
// effect/output/trigger/input labels - see Label.h) without rewriting
// this state machine per label type.
// Belongs to: StageLink_Common (shared by TX and RX).

#pragma once

#include "Label.h"

namespace StageLink
{
    class LabelEditor
    {
    public:
        // Begins editing, seeding the buffer from currentValue (not a
        // blank slate) so rotating starts from where the label already
        // is, and space-padding any remaining positions - same
        // "continue from where it is" behavior the old 2-char unit name
        // editor had.
        void begin(const char *currentValue)
        {
            setLabelText(buffer_, LABEL_BUFFER_SIZE, currentValue);

            size_t len = strlen(buffer_);
            for (size_t i = len; i < LABEL_MAX_LENGTH; ++i)
            {
                buffer_[i] = ' ';
            }
            buffer_[LABEL_MAX_LENGTH] = '\0';

            cursor_ = 0;
            active_ = true;
            cancelled_ = false;
        }

        bool isActive() const { return active_; }
        bool wasCancelled() const { return cancelled_; }
        const char *buffer() const { return buffer_; }
        uint8_t cursor() const { return cursor_; }

        // Encoder rotate: steps the character at the cursor through
        // LABEL_CHARSET, wrapping in either direction.
        void stepChar(int direction)
        {
            if (!active_)
            {
                return;
            }

            uint8_t index = labelCharsetIndexOf(buffer_[cursor_]);
            index = static_cast<uint8_t>(
                (index + LABEL_CHARSET_LENGTH + (direction > 0 ? 1 : -1)) % LABEL_CHARSET_LENGTH
            );
            buffer_[cursor_] = LABEL_CHARSET[index];
        }

        // Encoder press: confirms the character at the cursor and moves
        // on. Returns true once the last position has just been
        // confirmed - the caller's cue to read buffer(), trim it, and
        // save; isActive() is false immediately after.
        bool confirmChar()
        {
            if (!active_)
            {
                return false;
            }

            if (cursor_ + 1 < LABEL_MAX_LENGTH)
            {
                ++cursor_;
                return false;
            }

            active_ = false;
            return true;
        }

        // Back: steps back to re-edit the previous character. At the
        // first character, cancels the whole edit instead - isActive()
        // becomes false and wasCancelled() true, and the caller should
        // discard buffer() rather than saving it.
        void back()
        {
            if (!active_)
            {
                return;
            }

            if (cursor_ == 0)
            {
                active_ = false;
                cancelled_ = true;
                return;
            }

            --cursor_;
        }

    private:
        char buffer_[LABEL_BUFFER_SIZE] = {};
        uint8_t cursor_ = 0;
        bool active_ = false;
        bool cancelled_ = false;
    };
}
