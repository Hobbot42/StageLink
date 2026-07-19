#include <Arduino.h>
#include "Display.h"
#include "ReliableRadio.h"
#include "StatusLED.h"
#include "Button.h"
#include "Encoder.h"

namespace
{
uint8_t receiverAddress[] = {
    0x68, 0x09, 0x47, 0x3C, 0x49, 0xB4
};

constexpr uint8_t ENCODER_CLK_PIN = 32;
constexpr uint8_t ENCODER_DT_PIN = 33;
constexpr uint8_t ENCODER_BUTTON_PIN = 25;

unsigned long lastHeartbeatTime = 0;
bool buttonWasPressed = false;
StageLink::ReliableRadio radio;

void encodeEncoderPosition(int position, uint8_t *payload)
{
    uint32_t value = static_cast<uint32_t>(position);

    payload[0] = value & 0xFF;
    payload[1] = (value >> 8) & 0xFF;
    payload[2] = (value >> 16) & 0xFF;
    payload[3] = (value >> 24) & 0xFF;
}
}

void setup()
{
    Serial.begin(115200);

    StatusLED::begin();

    Button::begin();

    Encoder::begin(
        ENCODER_CLK_PIN,
        ENCODER_DT_PIN,
        ENCODER_BUTTON_PIN
    );

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
    Encoder::update();

    if (Encoder::consumeTurn() != 0)
    {
        Serial.print("Encoder: ");
        Serial.println(Encoder::position());

        uint8_t encoderPayload[4];
        encodeEncoderPosition(Encoder::position(), encoderPayload);

        if (radio.send(
                StageLink::PacketType::ENCODER_VALUE,
                encoderPayload,
                sizeof(encoderPayload)
            ))
        {
            Serial.println("Queued encoder update");
        }
        else
        {
            Serial.println("Encoder queue full");
        }
    }

    if (Encoder::buttonPressed())
    {
        Serial.println("Button Pressed");
    }

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
