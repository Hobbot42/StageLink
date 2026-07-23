// StageLink Label
// Generic fixed-size, device-editable text label used throughout FxQ so
// an operator can name things meaningfully without a PC or phone - a
// unit, and eventually an effect/output/trigger/input (see
// StageLink_Rx/src/main.cpp for the first user: the unit label).
// Deliberately just the data/charset/validation layer - persistence
// (ConfigManager), wire format (DeviceInfo.h), the encoder edit state
// machine (LabelEditor.h), and rendering (Display) are each a separate
// concern layered on top of this, so a future label type only needs to
// plug into those, not redefine what a label is.
// Belongs to: StageLink_Common (shared by TX and RX).

#pragma once

#include <Arduino.h>

namespace StageLink
{
    constexpr uint8_t LABEL_MAX_LENGTH = 12;
    constexpr uint8_t LABEL_BUFFER_SIZE = LABEL_MAX_LENGTH + 1; // + null terminator

    // Scroll order for encoder character entry: space first (the fill/
    // default character for untouched positions - see LabelEditor::begin),
    // then A-Z, 0-9, and the three punctuation characters, in that exact
    // order per the label spec. Uppercase only - no lowercase letters.
    constexpr char LABEL_CHARSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
    constexpr uint8_t LABEL_CHARSET_LENGTH = sizeof(LABEL_CHARSET) - 1; // exclude the string's own null terminator

    constexpr char LABEL_DEFAULT[LABEL_BUFFER_SIZE] = "LABEL";

    inline bool isValidLabelChar(char c)
    {
        return c != '\0' && strchr(LABEL_CHARSET, c) != nullptr;
    }

    // Index of c within LABEL_CHARSET, defaulting to 0 (space) if c isn't
    // a valid label character - keeps callers from having to check first.
    inline uint8_t labelCharsetIndexOf(char c)
    {
        const char *pos = strchr(LABEL_CHARSET, c);
        return pos != nullptr ? static_cast<uint8_t>(pos - LABEL_CHARSET) : 0;
    }

    // Copies src into a fixed-size label field, always null-terminating
    // even if src is longer than the field (truncates rather than
    // overflowing). Same convention as setDeviceInfoField (DeviceInfo.h).
    inline void setLabelText(char *field, uint8_t fieldSize, const char *src)
    {
        strncpy(field, src, fieldSize - 1);
        field[fieldSize - 1] = '\0';
    }

    // Strips trailing spaces in place. Labels shorter than
    // LABEL_MAX_LENGTH are space-padded while editing (space is charset
    // index 0, the fill character for positions the operator hasn't
    // typed over) - this trims that padding once the edit is committed,
    // so what's stored/displayed/transmitted is a clean C string.
    inline void trimTrailingLabelSpaces(char *text)
    {
        size_t len = strlen(text);
        while (len > 0 && text[len - 1] == ' ')
        {
            text[--len] = '\0';
        }
    }
}
