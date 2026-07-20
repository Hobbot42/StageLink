#include <Arduino.h>
#include "Display.h"
#include "ReliableRadio.h"
#include "StatusLED.h"
#include "Button.h"
#include "Encoder.h"
#include "StatusPageCycler.h"

namespace
{
uint8_t receiverAddress[] = {
    0x68, 0x09, 0x47, 0x3C, 0x49, 0xB4
};

constexpr uint8_t ENCODER_CLK_PIN = 32;
constexpr uint8_t ENCODER_DT_PIN = 33;
constexpr uint8_t ENCODER_BUTTON_PIN = 25;

enum class TxPage : uint8_t
{
    Status = 0,
    Diagnostics = 1
};
constexpr uint8_t TX_PAGE_COUNT = 2;
constexpr unsigned long DISPLAY_REFRESH_INTERVAL_MS = 250;

unsigned long lastHeartbeatTime = 0;
unsigned long lastDisplayRefresh = 0;
bool buttonWasPressed = false;
bool inputStateSnapshotNeeded = true;
bool displayReady = false;
StageLink::StatusPageCycler pageCycler;
StageLink::ReliableRadio radio;

void showCurrentPage()
{
    if (!displayReady)
    {
        return;
    }

    const char *linkState = radio.isPeerOnline() ? "CONNECTED" : "LINK LOST";

    if (pageCycler.page() == static_cast<uint8_t>(TxPage::Status))
    {
        Display::showStatus(
            linkState,
            Encoder::position(),
            Encoder::isButtonPressed()
        );
    }
    else
    {
        StageLink::RadioDiagnostics diagnostics = radio.diagnostics();

        Display::showDiagnostics(
            linkState,
            diagnostics.lastRoundTripMs,
            diagnostics.averageRoundTripMs,
            diagnostics.maxRoundTripMs,
            diagnostics.lastRetryCount,
            diagnostics.packetsFailed,
            diagnostics.rssiAvailable,
            diagnostics.rssi
        );
    }
}

void encodeEncoderPosition(int position, uint8_t *payload)
{
    uint32_t value = static_cast<uint32_t>(position);

    payload[0] = value & 0xFF;
    payload[1] = (value >> 8) & 0xFF;
    payload[2] = (value >> 16) & 0xFF;
    payload[3] = (value >> 24) & 0xFF;
}

void sendEncoderValue()
{
    uint8_t valuePayload[StageLink::VALUE_UPDATE_PAYLOAD_SIZE];
    StageLink::encodeValueUpdate(
        StageLink::ValueChannel::Encoder,
        Encoder::position(),
        valuePayload
    );

    if (radio.send(
            StageLink::PacketType::VALUE_UPDATE,
            valuePayload,
            sizeof(valuePayload)
        ))
    {
        Serial.println("Queued encoder update");
    }
    else
    {
        Serial.println("Encoder queue full");
    }
}

void sendEncoderButtonState(bool pressed)
{
    uint8_t buttonPayload[] =
    {
        1,
        static_cast<uint8_t>(
            pressed ?
                StageLink::ButtonState::Pressed :
                StageLink::ButtonState::Released
        )
    };

    if (radio.send(
            StageLink::PacketType::BUTTON_EVENT,
            buttonPayload,
            sizeof(buttonPayload)
        ))
    {
        Serial.print("Encoder button: ");
        Serial.println(pressed ? "PRESSED" : "RELEASED");
    }
    else
    {
        Serial.println("Button queue full");
    }
}

void sendCurrentInputState()
{
    Serial.println("Sending input state snapshot");

    uint8_t snapshotPayload[5];
    encodeEncoderPosition(Encoder::position(), snapshotPayload);
    snapshotPayload[4] = static_cast<uint8_t>(
        Encoder::isButtonPressed() ?
            StageLink::ButtonState::Pressed :
            StageLink::ButtonState::Released
    );

    if (!radio.send(
            StageLink::PacketType::STATE_SNAPSHOT,
            snapshotPayload,
            sizeof(snapshotPayload)
        ))
    {
        Serial.println("State snapshot queue full");
    }
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

    displayReady = Display::begin();

    if (!displayReady)
    {
        Serial.println("Display failed");
    }
    else
    {
        Display::showReady();
        Serial.println("Display Ready");
    }

    pageCycler.begin(TX_PAGE_COUNT);

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

    StageLink::Packet incomingPacket;
    while (radio.receive(incomingPacket))
    {
        if (incomingPacket.type == StageLink::PacketType::STATE_REQUEST)
        {
            Serial.println("State request received");
            sendCurrentInputState();
        }
    }

    if (Encoder::consumeTurn() != 0)
    {
        Serial.print("Encoder: ");
        Serial.println(Encoder::position());

        sendEncoderValue();
        showCurrentPage();
    }

    if (Encoder::buttonPressed())
    {
        pageCycler.next();
        showCurrentPage();
    }

    bool encoderButtonPressed;
    if (Encoder::consumeButtonStateChange(encoderButtonPressed))
    {
        sendEncoderButtonState(encoderButtonPressed);
        showCurrentPage();
    }

    StageLink::SendResult result;
    while (radio.getSendResult(result))
    {
        if (result.status == StageLink::SendStatus::Acknowledged)
        {
            Serial.print(StageLink::packetTypeName(result.type));
            Serial.print(" acknowledged, sequence: ");
            Serial.println(result.sequence);

            if (result.type == StageLink::PacketType::Heartbeat &&
                inputStateSnapshotNeeded)
            {
                sendCurrentInputState();
                inputStateSnapshotNeeded = false;
            }
        }
        else
        {
            Serial.print(StageLink::packetTypeName(result.type));
            Serial.print(" failed, sequence: ");
            Serial.println(result.sequence);

            if (result.type == StageLink::PacketType::Heartbeat)
            {
                inputStateSnapshotNeeded = true;
            }
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

    if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_INTERVAL_MS)
    {
        showCurrentPage();
        lastDisplayRefresh = millis();
    }

    delay(10);
}
