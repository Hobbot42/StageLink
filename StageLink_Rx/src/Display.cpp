#include "Display.h"

#include <Wire.h>
#include <U8g2lib.h>
#include "RxQInfo.h"
#include "FxQBuildInfo.h"

// FxQ
// Product: RxQ
// Version: v0.9.0
//
// Project information is maintained in FxQInfo.h

namespace
{
    constexpr int SDA_PIN = 21;
    constexpr int SCL_PIN = 22;

    // SH1106 128x64 OLED via U8g2 (full-buffer, hardware I2C). The
    // board's actual controller is an SH1106, not SSD1306 - they're
    // pin/size-compatible but use slightly different init/addressing,
    // which is why driving this panel with the old Adafruit_SSD1306
    // code produced a corrupted display (line 1 readable, everything
    // after it garbage/vertical noise).
    U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0);

    // u8g2_font_5x7_tf: a fixed 5x7 bitmap font, 8px line pitch (7px
    // glyph + 1px gap) - matches the old Adafruit_GFX default font
    // closely enough that every "8 rows fit on this 64px screen"
    // assumption elsewhere in this file still holds without
    // re-deriving row budgets.
    constexpr uint8_t SMALL_LINE_HEIGHT = 8;
    constexpr uint8_t SMALL_BASELINE = 7; // font ascent - first row's glyph tops sit at y=0

    // u8g2_font_8x13_tf for the handful of "one big value" screens.
    // Deliberately sized to consume exactly 2 small-font row-units
    // (16px) so mixing it with printLine() calls stays on the row grid
    // - see printLargeLine().
    constexpr uint8_t LARGE_LINE_HEIGHT = 16;
    constexpr uint8_t LARGE_BASELINE = 12;

    // Advanced by printLine()/printLargeLine(), reset by beginScreen() -
    // tracks the next unused small-font row (0-7) so callers don't have
    // to compute y-coordinates themselves.
    uint8_t nextRow = 0;

    void beginScreen()
    {
        display.clearBuffer();
        display.setFont(u8g2_font_5x7_tf);
        nextRow = 0;
    }

    void endScreen()
    {
        display.sendBuffer();
    }

    // Draws text on the next small-font row and advances - same
    // ergonomics as the old Adafruit_GFX println() chain, but positions
    // each line explicitly with its own setCursor() rather than relying
    // on U8g2's own handling of '\n' within a single print() call, so
    // row placement stays exact and predictable. Call with no argument
    // for a blank line.
    void printLine(const char *text = "")
    {
        display.setCursor(0, SMALL_BASELINE + nextRow * SMALL_LINE_HEIGHT);
        display.print(text);
        nextRow++;
    }

    // Draws one large-font line right after whatever small-font rows
    // came before it on this screen, then restores the small font and
    // advances nextRow by the 2 row-units the large line occupies -
    // callers can keep calling printLine() afterward without adjusting
    // anything themselves.
    void printLargeLine(const char *text)
    {
        display.setFont(u8g2_font_8x13_tf);
        display.setCursor(0, nextRow * SMALL_LINE_HEIGHT + LARGE_BASELINE);
        display.print(text);
        display.setFont(u8g2_font_5x7_tf);
        nextRow += LARGE_LINE_HEIGHT / SMALL_LINE_HEIGHT;
    }

    // Draws a framed signal-strength bar on the next small-font row's
    // band and advances nextRow by one - graphical rather than text
    // since the small bitmap font has no block-drawing characters to
    // render "flags" with. Empty (frame only, no fill) when RSSI isn't
    // available. rssi is clamped to a rough -90 (empty) .. -30 (full)
    // dBm range - not calibrated against any spec, just enough spread
    // to look meaningful.
    void drawSignalBar(bool rssiAvailable, int8_t rssi)
    {
        constexpr int BAR_X = 0;
        constexpr int BAR_WIDTH = 100;
        constexpr int BAR_HEIGHT = 6;
        constexpr int RSSI_MIN = -90;
        constexpr int RSSI_MAX = -30;

        int barY = nextRow * SMALL_LINE_HEIGHT;
        display.drawFrame(BAR_X, barY, BAR_WIDTH, BAR_HEIGHT);

        if (rssiAvailable)
        {
            int clamped = rssi;
            if (clamped < RSSI_MIN) clamped = RSSI_MIN;
            if (clamped > RSSI_MAX) clamped = RSSI_MAX;

            int quality = (clamped - RSSI_MIN) * 100 / (RSSI_MAX - RSSI_MIN);
            int innerWidth = BAR_WIDTH - 2;
            int fillWidth = innerWidth * quality / 100;

            if (fillWidth > 0)
            {
                display.drawBox(BAR_X + 1, barY + 1, fillWidth, BAR_HEIGHT - 2);
            }
        }

        nextRow++;
    }

    // Formats "QNN" or, when name is non-null/non-empty, "QNN Name" - no
    // trailing space in the nameless case. "Q" rather than "CUE-" so the
    // combined text fits the large font's ~16-character width on Show
    // Mode's current/next lines (see showGuiShowRun()) - "Q04 Dragon
    // Move" is 15 characters, "CUE-04 Dragon Move" would have been 19.
    void formatCueLabel(char *buffer, size_t bufferSize, int cueNumber, const char *name)
    {
        if (name != nullptr && name[0] != '\0')
        {
            snprintf(buffer, bufferSize, "Q%02d %s", cueNumber, name);
        }
        else
        {
            snprintf(buffer, bufferSize, "Q%02d", cueNumber);
        }
    }

    // Appended to every screen so the firmware actually running on the
    // board is always visible - see FxQInfo.h/FxQBuildInfo.h for the
    // underlying values.
    void printIdentityLine()
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%s %s B%d", FxQ::PRODUCT_NAME, FxQ::PRODUCT_VERSION, FXQ_BUILD_NUMBER);
        printLine(buffer);
    }
}

bool Display::begin()
{
    Wire.begin(SDA_PIN, SCL_PIN);

    if (!display.begin())
    {
        return false;
    }

    display.clearBuffer();
    display.sendBuffer();

    return true;
}

void Display::showEncoderValue(int value)
{
    beginScreen();

    printLine(FxQ::PRODUCT_NAME);
    printLine();
    printLine("Encoder:");

    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%d", value);
    printLargeLine(buffer);

    endScreen();
}

void Display::showButtonState(bool pressed)
{
    beginScreen();

    printLine(FxQ::PRODUCT_NAME);
    printLine();
    printLine("Button:");
    printLargeLine(pressed ? "PRESSED" : "RELEASED");

    endScreen();
}

void Display::showLinkState(const char *state)
{
    beginScreen();

    printLine(FxQ::PRODUCT_NAME);
    printLine();
    printLine("Link:");
    printLargeLine(state);

    endScreen();
}

void Display::showLinkAndEncoder(
    const char *linkState,
    int encoderValue,
    bool buttonPressed,
    int localEncoderValue,
    int servoAngle
)
{
    beginScreen();
    printIdentityLine();

    char buffer[32];

    snprintf(buffer, sizeof(buffer), "Link: %s", linkState);
    printLine(buffer);

    snprintf(buffer, sizeof(buffer), "TX Encoder: %d", encoderValue);
    printLine(buffer);

    snprintf(buffer, sizeof(buffer), "TX Button: %s", buttonPressed ? "PRESSED" : "RELEASED");
    printLine(buffer);

    snprintf(buffer, sizeof(buffer), "RX Encoder: %d", localEncoderValue);
    printLine(buffer);

    snprintf(buffer, sizeof(buffer), "Servo: %d deg", servoAngle);
    printLine(buffer);

    endScreen();
}

void Display::showDiagnostics(
    const char *linkState,
    bool rssiAvailable,
    int8_t rssi,
    uint32_t packetCount,
    uint32_t retryErrorCount,
    const uint8_t deviceId[StageLink::DEVICE_ID_LENGTH]
)
{
    beginScreen();
    printIdentityLine();
    printLine("Diagnostics");

    char idBuffer[StageLink::DEVICE_ID_SHORT_STRING_LENGTH];
    StageLink::formatDeviceIdShort(deviceId, idBuffer, sizeof(idBuffer));

    char buffer[32];

    snprintf(buffer, sizeof(buffer), "ID: %s", idBuffer);
    printLine(buffer);

    snprintf(buffer, sizeof(buffer), "Link: %s", linkState);
    printLine(buffer);

    if (rssiAvailable)
    {
        snprintf(buffer, sizeof(buffer), "TX RSSI: %d dBm", rssi);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "TX RSSI: --");
    }
    printLine(buffer);

    snprintf(buffer, sizeof(buffer), "Packets: %lu", static_cast<unsigned long>(packetCount));
    printLine(buffer);

    snprintf(buffer, sizeof(buffer), "Retry/Err: %lu", static_cast<unsigned long>(retryErrorCount));
    printLine(buffer);

    endScreen();
}

void Display::showUnitLabel(const char *unitLabel)
{
    beginScreen();
    printIdentityLine();
    printLine("Controller Label");
    printLine();
    printLine(unitLabel);
    printLine();
    printLine("Hold to edit");

    endScreen();
}

void Display::showUnitLabelEdit(const char *buffer, uint8_t cursor, bool useBackButton)
{
    beginScreen();
    printLine("Edit Controller Label");
    printLine();
    printLine(buffer);

    // Caret under the character the encoder currently edits - the font
    // is monospace, so padding with spaces lines it up without
    // measuring pixel widths.
    char caretLine[StageLink::LABEL_MAX_LENGTH + 2];
    for (uint8_t i = 0; i < cursor; ++i)
    {
        caretLine[i] = ' ';
    }
    caretLine[cursor] = '^';
    caretLine[cursor + 1] = '\0';
    printLine(caretLine);

    printLine();

    char charLine[32];
    snprintf(charLine, sizeof(charLine), "Char %d of %d", cursor + 1, StageLink::LABEL_MAX_LENGTH);
    printLine(charLine);

    // The GUI shows no gesture hint - the controls are consistent across
    // every screen, so a per-screen reminder is just a used row. The
    // legacy diagnostic pages keep theirs: hold is not obvious there.
    if (!useBackButton)
    {
        printLine("Press=next Hold=back");
    }

    endScreen();
}

void Display::showEffectTest(bool running, bool storedAtBoot)
{
    beginScreen();
    printIdentityLine();
    printLine("Effect Test");
    printLine();

    printLine("Effect 1");
    printLine(storedAtBoot ? "Stored OK" : "Default Loaded");
    printLine();

    printLine(running ? "Running" : "No Effect");

    endScreen();
}

void Display::showTriggerStatus(const char *lastTriggerLabel, int effectNumber)
{
    beginScreen();
    printIdentityLine();
    printLine("Trigger");
    printLine();

    printLine("Last:");
    printLine(lastTriggerLabel);
    printLine();

    printLine("Effect:");
    if (effectNumber > 0)
    {
        char buffer[12];
        snprintf(buffer, sizeof(buffer), "%d", effectNumber);
        printLine(buffer);
    }
    else
    {
        printLine("-");
    }

    endScreen();
}

void Display::showGuiList(
    const char *title,
    const char *contextLabel,
    const char *const *items,
    uint8_t itemCount,
    uint8_t selectedIndex
)
{
    beginScreen();
    printLine(title);

    // 8 rows fit at this font. title + the blank separator always cost
    // 2; contextLabel (when present) costs a 3rd, leaving the rest for
    // scrollable item rows.
    uint8_t usedRows = 2;
    if (contextLabel != nullptr)
    {
        printLine(contextLabel);
        usedRows++;
    }
    printLine();

    uint8_t visibleRows = (usedRows < 8) ? (8 - usedRows) : 0;

    // Scrolls the window so the selection is always visible rather than
    // always starting the list at item 0.
    uint8_t firstVisible = 0;
    if (itemCount > visibleRows && selectedIndex >= visibleRows)
    {
        firstVisible = selectedIndex - visibleRows + 1;
    }

    for (uint8_t i = 0; i < visibleRows && (firstVisible + i) < itemCount; ++i)
    {
        uint8_t itemIndex = firstVisible + i;

        char lineBuffer[32];
        snprintf(
            lineBuffer, sizeof(lineBuffer), "%s%s",
            itemIndex == selectedIndex ? "> " : "  ",
            items[itemIndex]
        );
        printLine(lineBuffer);
    }

    endScreen();
}

void Display::showGuiShowRun(
    const char *deviceLabel,
    const char *showName,
    uint8_t currentCue,
    const char *currentCueName,
    uint8_t nextCue,
    const char *nextCueName,
    bool linkOnline,
    bool rssiAvailable,
    int8_t rssi
)
{
    beginScreen();

    printLine(deviceLabel);

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "Show: %s", showName);
    printLine(buffer);

    char cueLabel[32];

    // "-" marks the cue that last ran, ">" the one GO will fire next.
    // The only place a dash is used - the list screens show the cursor
    // alone, since "which row is selected" is the only state they carry.
    formatCueLabel(cueLabel, sizeof(cueLabel), currentCue, currentCueName);
    snprintf(buffer, sizeof(buffer), "-%s", cueLabel);
    printLargeLine(buffer);

    formatCueLabel(cueLabel, sizeof(cueLabel), nextCue, nextCueName);
    snprintf(buffer, sizeof(buffer), ">%s", cueLabel);
    printLargeLine(buffer);

    if (linkOnline)
    {
        drawSignalBar(rssiAvailable, rssi);
    }
    else
    {
        // No bar to draw when the link's down - text instead, same row.
        // Automatically reverts to the bar on its own next time render()
        // passes linkOnline = true; nothing here remembers the lost
        // state.
        printLine("LINK LOST");
    }

    endScreen();
}

void Display::showGuiValueEntry(
    const char *title,
    const char *contextLabel,
    const char *fieldLabel,
    const char *valueText,
    uint8_t fieldIndex,
    uint8_t fieldCount
)
{
    beginScreen();
    printLine(title);
    printLine(contextLabel);
    printLine();

    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%s:", fieldLabel);
    printLine(buffer);

    // Size 2 for the value itself - the single most important thing on
    // this screen - same "big number" convention as showEncoderValue.
    printLargeLine(valueText);

    snprintf(buffer, sizeof(buffer), "Field %d of %d", fieldIndex + 1, fieldCount);
    printLine(buffer);


    endScreen();
}

void Display::showUpdateMode(
    const char *statusLine,
    const char *ipAddress,
    int secondsRemaining
)
{
    beginScreen();
    printIdentityLine();
    printLine("UPDATE MODE");
    printLine();

    printLine(statusLine);
    printLine();

    if (ipAddress != nullptr && ipAddress[0] != '\0')
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "IP: %s", ipAddress);
        printLine(buffer);
    }
    else
    {
        printLine("IP: --");
    }

    if (secondsRemaining >= 0)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "Timeout: %ds", secondsRemaining);
        printLine(buffer);
    }

    endScreen();
}
