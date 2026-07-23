#include "Display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "TxQInfo.h"
#include "FxQBuildInfo.h"
#include "DeviceInfo.h"
#include "OutputList.h"

// FxQ
// Product: TxQ
// Version: v0.9.0
//
// Project information is maintained in FxQInfo.h

namespace
{
    constexpr int SCREEN_WIDTH = 128;
    constexpr int SCREEN_HEIGHT = 64;
    constexpr uint8_t OLED_ADDRESS = 0x3C;
    // I2C pins for the SSD1306 OLED.
    constexpr int SDA_PIN = 21;
    constexpr int SCL_PIN = 22;

    Adafruit_SSD1306 display(
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        &Wire,
        -1
    );

    // Appended to every screen so the firmware actually running on the
    // board is always visible - see FxQInfo.h/FxQBuildInfo.h for the
    // underlying values. Text wraps automatically (Adafruit_GFX default)
    // if it doesn't fit the current line.
    void printIdentityLine()
    {
        display.print(FxQ::PRODUCT_NAME);
        display.print(" ");
        display.print(FxQ::PRODUCT_VERSION);
        display.print(" B");
        display.println(FXQ_BUILD_NUMBER);
    }
}

bool Display::begin()
{
    Wire.begin(SDA_PIN, SCL_PIN);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS))
    {
        return false;
    }

    display.clearDisplay();
    display.display();

    return true;
}

void Display::showReady()
{
    display.clearDisplay();

    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(FxQ::PRODUCT_NAME);

    display.setTextSize(1);
    display.println();
    display.println("READY");
    printIdentityLine();

    display.display();
}

void Display::showConfigMode()
{
    display.clearDisplay();

    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("CONFIG");

    display.setTextSize(1);
    display.println();
    display.println("MODE");

    display.display();
}

void Display::showStatus(
    const char *linkState,
    int encoderValue,
    bool buttonPressed,
    const char *controlModeLabel,
    int controlModeValue
)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    printIdentityLine();

    display.print("Link: ");
    display.println(linkState);

    display.print("Encoder: ");
    display.println(encoderValue);

    // Whichever channel the encoder currently controls - see ControlMode
    // in main.cpp. The encoder button cycles this.
    display.print(controlModeLabel);
    display.print(": ");
    display.println(controlModeValue);

    display.print("Button: ");
    display.println(buttonPressed ? "PRESSED" : "RELEASED");

    display.display();
}

void Display::showDiagnostics(
    const char *linkState,
    unsigned long lastLatencyMs,
    unsigned long averageLatencyMs,
    unsigned long maxLatencyMs,
    uint8_t retryCount,
    uint32_t failedCount,
    bool rssiAvailable,
    int8_t rssi
)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    printIdentityLine();
    display.println("Diagnostics");

    display.print("Link: ");
    display.println(linkState);

    display.print("L/A/M: ");
    display.print(lastLatencyMs);
    display.print("/");
    display.print(averageLatencyMs);
    display.print("/");
    display.println(maxLatencyMs);

    display.print("RX RSSI: ");
    if (rssiAvailable)
    {
        display.print(rssi);
        display.println(" dBm");
    }
    else
    {
        display.println("--");
    }

    display.print("Retries: ");
    display.println(retryCount);

    display.print("Failed: ");
    display.println(failedCount);

    display.display();
}

void Display::showDeviceInfo(const RemoteDevice &remote)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    // This page identifies itself rather than using printIdentityLine()
    // like the other pages - RX's info is the whole point of this page,
    // and the build/version identity is only a few button-presses away
    // on the Status page, so it isn't worth the line budget here (see
    // the row-count comment below).
    display.println("REMOTE DEVICE");
    display.println();

    // Three states, matching how far discovery has actually gotten -
    // see RemoteDevice.h. Nothing here is stored separately from
    // RemoteDevice; this just decides how to word what it already has.
    if (!remote.connected)
    {
        display.println("RX: Offline");
        display.display();
        return;
    }

    if (!remote.hasDeviceInfo || !remote.hasOutputList)
    {
        display.println("RX: Connected");
        display.println("Waiting...");
        display.display();
        return;
    }

    display.print("Unit: ");
    display.println(remote.info.unitLabel);

    display.println(remote.info.name);

    char idBuffer[StageLink::DEVICE_ID_SHORT_STRING_LENGTH];
    StageLink::formatDeviceIdShort(remote.info.deviceId, idBuffer, sizeof(idBuffer));
    display.print("ID: ");
    display.println(idBuffer);

    display.print("FW: ");
    display.println(remote.info.firmwareVersion);

    // This 128x64 OLED at text size 1 fits 8 rows. Title/blank/Unit/name/
    // ID/FW/header already use 7 (no second blank line - see the earlier
    // ID-line comment history), leaving only 1 for individual output
    // lines now that Unit took the last spare row. When there are more
    // outputs than that, the header shows the total count instead of
    // adding a "+N more" row - there isn't room for both, and the spec
    // calls for showing the count rather than scrolling.
    constexpr uint8_t MAX_DISPLAYED_OUTPUTS = 1;
    uint8_t shown = remote.outputs.count < MAX_DISPLAYED_OUTPUTS
        ? remote.outputs.count
        : MAX_DISPLAYED_OUTPUTS;

    if (remote.outputs.count > MAX_DISPLAYED_OUTPUTS)
    {
        display.print("Outputs: ");
        display.println(remote.outputs.count);
    }
    else
    {
        display.println("Outputs:");
    }

    for (uint8_t i = 0; i < shown; ++i)
    {
        const StageLink::OutputDescriptor &descriptor = remote.outputs.outputs[i];
        display.print(StageLink::capabilityName(descriptor.capability));
        display.print(" #");
        display.println(descriptor.index);
    }

    display.display();
}