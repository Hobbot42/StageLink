#include "Display.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// StageLink Display (RX)

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
    display.println("StageLink RX");

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
    display.println("StageLink RX");

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
    display.println("StageLink RX");

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
    display.println("StageLink RX");

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
    uint32_t retryErrorCount
)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("RX Diagnostics");

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
