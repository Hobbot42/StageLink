#include <Arduino.h>
#include "Display.h"
#include "ReliableRadio.h"
#include "StatusLED.h"

namespace
{
unsigned long cueFlashUntil = 0;
int encoderValue = 0;
bool displayReady = false;
StageLink::ReliableRadio radio;

int decodeEncoderPosition(const StageLink::StagePacket &packet)
{
    uint32_t value =
        static_cast<uint8_t>(packet.payload[0]) |
        (static_cast<uint32_t>(static_cast<uint8_t>(packet.payload[1])) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(packet.payload[2])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(packet.payload[3])) << 24);

    return static_cast<int32_t>(value);
}
}

void setup()
{
    Serial.begin(115200);

    StatusLED::begin();
    StatusLED::setOffline();

    displayReady = Display::begin();
    if (!displayReady)
    {
        Serial.println("Display failed");
    }
    else
    {
        Display::showEncoderValue(encoderValue);
        Serial.println("Display Ready");
    }

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
        else if (packet.type == StageLink::PacketType::ENCODER_VALUE &&
                 packet.payloadLength == 4)
        {
            encoderValue = decodeEncoderPosition(packet);

            Serial.print("Encoder received: ");
            Serial.println(encoderValue);

            if (displayReady)
            {
                Display::showEncoderValue(encoderValue);
            }
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
