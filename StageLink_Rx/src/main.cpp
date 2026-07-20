#include <Arduino.h>
#include "Display.h"
#include "ReliableRadio.h"
#include "StatusLED.h"
#include "StatusPageCycler.h"
#include "Encoder.h"

namespace
{
enum class LinkState : uint8_t
{
    WaitingForLink,
    Connected,
    LinkLost
};

enum class RxPage : uint8_t
{
    Status = 0,
    Diagnostics = 1
};
constexpr uint8_t RX_PAGE_COUNT = 2;
constexpr unsigned long DISPLAY_REFRESH_INTERVAL_MS = 250;

constexpr uint8_t ENCODER_CLK_PIN = 32;
constexpr uint8_t ENCODER_DT_PIN = 33;
constexpr uint8_t ENCODER_BUTTON_PIN = 25;

unsigned long cueFlashUntil = 0;
unsigned long lastDisplayRefresh = 0;
int encoderValue = 0;
bool encoderButtonPressed = false;
bool displayReady = false;
bool hasReceivedValidPacket = false;
LinkState linkState = LinkState::WaitingForLink;
StageLink::StatusPageCycler pageCycler;
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

const char *linkStateText()
{
    switch (linkState)
    {
        case LinkState::WaitingForLink:
            return "WAITING";
        case LinkState::Connected:
            return "CONNECTED";
        case LinkState::LinkLost:
            return "LINK LOST";
        default:
            return "UNKNOWN";
    }
}

void showCurrentPage()
{
    if (!displayReady)
    {
        return;
    }

    if (pageCycler.page() == static_cast<uint8_t>(RxPage::Status))
    {
        Display::showLinkAndEncoder(
            linkStateText(),
            encoderValue,
            encoderButtonPressed,
            Encoder::position()
        );
    }
    else
    {
        StageLink::RadioDiagnostics diagnostics = radio.diagnostics();

        Display::showDiagnostics(
            linkStateText(),
            diagnostics.rssiAvailable,
            diagnostics.rssi,
            diagnostics.packetsReceived,
            diagnostics.totalRetries + diagnostics.packetsFailed
        );
    }
}

void setLinkState(LinkState newState, bool forceDisplayUpdate = false)
{
    if (!forceDisplayUpdate && newState == linkState)
    {
        return;
    }

    linkState = newState;

    if (linkState == LinkState::Connected)
    {
        StatusLED::setReady();
    }
    else
    {
        StatusLED::setOffline();
    }

    Serial.print("Link state: ");
    Serial.println(linkStateText());
    showCurrentPage();
}

void updateLinkState()
{
    if (!hasReceivedValidPacket)
    {
        setLinkState(LinkState::WaitingForLink);
    }
    else if (radio.isPeerOnline())
    {
        setLinkState(LinkState::Connected);
    }
    else
    {
        setLinkState(LinkState::LinkLost);
    }
}
}

void setup()
{
    Serial.begin(115200);

    StatusLED::begin();
    StatusLED::setOffline();

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
        Serial.println("Display Ready");
    }

    pageCycler.begin(RX_PAGE_COUNT);

    setLinkState(LinkState::WaitingForLink, true);

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
    Encoder::update();

    if (Encoder::buttonPressed())
    {
        pageCycler.next();
        showCurrentPage();
    }

    StageLink::Packet packet;
    while (radio.receive(packet))
    {
        if (!hasReceivedValidPacket)
        {
            hasReceivedValidPacket = true;

            if (radio.send(StageLink::PacketType::STATE_REQUEST))
            {
                Serial.println("Requested input state");
            }
            else
            {
                Serial.println("State request queue full");
            }
        }

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
                showCurrentPage();
            }
        }
        else if (packet.type == StageLink::PacketType::BUTTON_EVENT &&
                 packet.payloadLength == 2 &&
                 packet.payload[0] == 1 &&
                 (packet.payload[1] == static_cast<char>(StageLink::ButtonState::Pressed) ||
                  packet.payload[1] == static_cast<char>(StageLink::ButtonState::Released)))
        {
            encoderButtonPressed =
                packet.payload[1] == static_cast<char>(StageLink::ButtonState::Pressed);

            Serial.print("Encoder button: ");
            Serial.println(encoderButtonPressed ? "PRESSED" : "RELEASED");

            showCurrentPage();
        }
        else if (packet.type == StageLink::PacketType::STATE_SNAPSHOT &&
                 packet.payloadLength == 5 &&
                 (packet.payload[4] == static_cast<char>(StageLink::ButtonState::Pressed) ||
                  packet.payload[4] == static_cast<char>(StageLink::ButtonState::Released)))
        {
            encoderValue = decodeEncoderPosition(packet);
            encoderButtonPressed =
                packet.payload[4] == static_cast<char>(StageLink::ButtonState::Pressed);

            Serial.print("State snapshot received: encoder ");
            Serial.print(encoderValue);
            Serial.print(", button ");
            Serial.println(encoderButtonPressed ? "PRESSED" : "RELEASED");

            showCurrentPage();
        }
        else
        {
            Serial.print("Packet received, type: ");
            Serial.println(static_cast<uint8_t>(packet.type));
        }
    }

    updateLinkState();

    if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_INTERVAL_MS)
    {
        showCurrentPage();
        lastDisplayRefresh = millis();
    }

    if (millis() < cueFlashUntil)
    {
        StatusLED::setConfig();
        return;
    }

}
