#include <Arduino.h>
#include "ReliableRadio.h"
#include "StatusLED.h"

namespace
{
unsigned long cueFlashUntil = 0;
StageLink::ReliableRadio radio;
}

void setup()
{
    Serial.begin(115200);

    StatusLED::begin();
    StatusLED::setOffline();

    Serial.println();
    Serial.println("StageLink RX");
    Serial.println("Starting reliable radio...");

    if (!radio.begin())
    {
        Serial.println("Radio failed");
        return;
    }

    Serial.println("RX Ready");
}


void loop()
{
    radio.update();

    StageLink::Packet packet;
    while (radio.receive(packet))
    {
        if (packet.type == StageLink::PacketType::Heartbeat)
        {
            Serial.print("Heartbeat received, sequence: ");
            Serial.println(packet.sequence);
        }
        else if (packet.type == StageLink::PacketType::Cue &&
                 packet.payloadLength == 1 &&
                 packet.payload[0] == '1')
        {
            Serial.print("Cue 1 received, sequence: ");
            Serial.println(packet.sequence);
            cueFlashUntil = millis() + 100;
        }
        else
        {
            Serial.print("Packet received, type: ");
            Serial.println(static_cast<uint8_t>(packet.type));
        }
    }

    if (millis() < cueFlashUntil)
    {
        StatusLED::setConfig();
        return;
    }

    if (!radio.isPeerOnline())
    {
        StatusLED::setOffline();
    }
    else
    {
        StatusLED::setReady();
    }
}
