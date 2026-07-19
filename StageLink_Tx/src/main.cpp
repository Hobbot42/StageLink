#include <Arduino.h>
#include "Display.h"
#include "ReliableRadio.h"
#include "StatusLED.h"
#include "Button.h"

namespace
{
uint8_t receiverAddress[] = {
    0x68, 0x09, 0x47, 0x3C, 0x49, 0xB4
};

unsigned long lastHeartbeatTime = 0;
bool buttonWasPressed = false;
StageLink::ReliableRadio radio;
}

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
    Serial.println("Starting reliable radio...");

    if (!radio.begin(receiverAddress))
    {
        Serial.println("Radio failed");
        return;
    }
}


void loop()
{
    radio.update();

    StageLink::SendResult result;
    while (radio.getSendResult(result))
    {
        if (result.status == StageLink::SendStatus::Acknowledged)
        {
            Serial.print(StageLink::packetTypeName(result.type));
            Serial.print(" acknowledged, sequence: ");
            Serial.println(result.sequence);
        }
        else
        {
            Serial.print(StageLink::packetTypeName(result.type));
            Serial.print(" failed, sequence: ");
            Serial.println(result.sequence);
        }
    }

    if (millis() - lastHeartbeatTime >= 2000)
    {
        if (radio.send(StageLink::PacketType::Heartbeat))
        {
            Serial.println("Queued heartbeat");
        }
        else
        {
            Serial.println("Heartbeat queue full");
        }

        lastHeartbeatTime = millis();
    }

    bool buttonPressed = Button::pressed();

    if (buttonPressed && !buttonWasPressed)
    {
        if (radio.send(StageLink::PacketType::Cue, "1"))
        {
            Serial.println("Queued cue 1");
        }
        else
        {
            Serial.println("Cue queue full");
        }

        Display::showConfigMode();
    }

    buttonWasPressed = buttonPressed;

    if (radio.isPeerOnline())
    {
        StatusLED::setReady();
    }
    else
    {
        StatusLED::setOffline();
    }

    delay(10);
}
