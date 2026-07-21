#include "Display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// StageLink Display (TX)

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

void Display::showReady()
{
    display.clearDisplay();

    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("StageLink");

    display.setTextSize(1);
    display.println();
    display.println("READY");

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
    int servoAngle
)
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("StageLink TX");

    display.print("Link: ");
    display.println(linkState);

    display.print("Encoder: ");
    display.println(encoderValue);

    display.print("Servo: ");
    display.print(servoAngle);
    display.println(" deg");

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
    display.println("TX Diagnostics");

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