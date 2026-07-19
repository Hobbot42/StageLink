#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "Display.h"
#include "Radio.h"
#include "StatusLED.h"
#include "Button.h"

uint8_t receiverAddress[] = {
    0x68, 0x09, 0x47, 0x3C, 0x49, 0xB4
};

void setup()
{
    Serial.begin(115200);

    StatusLED::begin();

    Button::begin();

    if (!Display::begin())
{
    Serial.println("Display failed");
}
else
{
    Display::showReady();
    Serial.println("Display Ready");
}

  Serial.println();
Serial.println("StageLink TX");

if (!Radio::begin())
{
    Serial.println("Radio failed");
    return;
}
}


void loop()
{
    const char message[] = "HELLO RX";

bool result = Radio::sendMessage(message);
if (result)
{
    Serial.println("Sent: HELLO RX");
}
else
{
    Serial.println("Send failed");
}

if (Button::pressed())
{
    StatusLED::setConfig();
    Display::showConfigMode();
}

    
 if (Radio::isReceiverOnline())
{
    StatusLED::setReady();
}
else
{
    StatusLED::setOffline();
}


    delay(2000);
}