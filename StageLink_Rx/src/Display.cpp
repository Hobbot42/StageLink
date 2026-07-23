#include "Display.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "RxQInfo.h"
#include "FxQBuildInfo.h"

// FxQ
// Product: RxQ
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

void Display::showEncoderValue(int value)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(FxQ::PRODUCT_NAME);

    display.setTextSize(1);
    display.println();
    display.println("Encoder:");

    display.setTextSize(2);
    display.println(value);

    display.display();
}

void Display::showButtonState(bool pressed)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(FxQ::PRODUCT_NAME);

    display.println();
    display.println("Button:");

    display.setTextSize(2);
    display.println(pressed ? "PRESSED" : "RELEASED");

    display.display();
}

void Display::showLinkState(const char *state)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(FxQ::PRODUCT_NAME);

    display.println();
    display.println("Link:");

    display.setTextSize(2);
    display.println(state);

    display.display();
}

void Display::showLinkAndEncoder(
    const char *linkState,
    int encoderValue,
    bool buttonPressed,
    int localEncoderValue,
    int servoAngle
)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    printIdentityLine();

    display.print("Link: ");
    display.println(linkState);

    display.print("TX Encoder: ");
    display.println(encoderValue);

    display.print("TX Button: ");
    display.println(buttonPressed ? "PRESSED" : "RELEASED");

    display.print("RX Encoder: ");
    display.println(localEncoderValue);

    display.print("Servo: ");
    display.print(servoAngle);
    display.println(" deg");

    display.display();
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
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    printIdentityLine();
    display.println("Diagnostics");

    char idBuffer[StageLink::DEVICE_ID_SHORT_STRING_LENGTH];
    StageLink::formatDeviceIdShort(deviceId, idBuffer, sizeof(idBuffer));
    display.print("ID: ");
    display.println(idBuffer);

    display.print("Link: ");
    display.println(linkState);

    display.print("TX RSSI: ");
    if (rssiAvailable)
    {
        display.print(rssi);
        display.println(" dBm");
    }
    else
    {
        display.println("--");
    }

    display.print("Packets: ");
    display.println(packetCount);

    display.print("Retry/Err: ");
    display.println(retryErrorCount);

    display.display();
}

void Display::showUnitName(const char *unitName)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    printIdentityLine();
    display.println("Unit Name");
    display.println();

    display.setTextSize(2);
    display.println(unitName);

    display.setTextSize(1);
    display.println();
    display.println("Hold to edit");

    display.display();
}

void Display::showUnitNameEdit(char char0, char char1, uint8_t editingCharIndex)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Edit Unit Name");
    display.println();

    display.setTextSize(2);
    display.print(char0);
    display.println(char1);

    display.setTextSize(1);
    display.println();
    display.print("Char ");
    display.print(editingCharIndex + 1);
    display.println(" of 2");
    display.println("Press to confirm");

    display.display();
}

void Display::showEffectTest(bool running, bool storedAtBoot)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    printIdentityLine();
    display.println("Effect Test");
    display.println();

    display.println("Effect 1");
    display.println(storedAtBoot ? "Stored OK" : "Default Loaded");
    display.println();

    display.println(running ? "Running" : "No Effect");

    display.display();
}
