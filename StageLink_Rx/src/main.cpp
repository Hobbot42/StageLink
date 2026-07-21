// StageLink RX (receiver / output unit)
// Receives TX's encoder/button state over the air and drives a physical
// servo to match. Also has its own local encoder button, used only to
// cycle its own display pages (not sent anywhere) - so "encoder" in this
// file means two different things depending on context: TX's remote
// value (encoderValue, received) vs. this board's own local encoder
// (Encoder::position(), read directly).
// Belongs to: StageLink_Rx.
//
// Packet flow (see TX main.cpp for the other side, ReliableRadio.h/
// StageLinkProtocol.h for the transport and wire format):
//   RX -> TX  STATE_REQUEST    sent once, right after the first packet
//                               is ever received (see hasReceivedValidPacket)
//   TX -> RX  STATE_SNAPSHOT   full resync reply, applied generically by
//                               channel/type (see StateSnapshot.h)
//   TX -> RX  VALUE_UPDATE     live encoder/servo value change
//   TX -> RX  BUTTON_EVENT     live encoder-button press/release
//   TX -> RX  Cue              momentary cue-button trigger (flashes LED)
//   TX -> RX  Heartbeat        periodic liveness signal, drives LinkState
#include <Arduino.h>
#include "Display.h"
#include "ReliableRadio.h"
#include "StatusLED.h"
#include "StatusPageCycler.h"
#include "Encoder.h"
#include "ServoOutput.h"
#include "StateSnapshot.h"
#include "ConfigManager.h"
#include "OutputManager.h"
#include "LEDOutput.h"

namespace
{
// Tracks link health for display/LED purposes; distinct from
// ReliableRadio::isPeerOnline(), which only reflects recent traffic -
// this also accounts for never having heard from TX at all yet.
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

// RX's own local encoder (button only used for local page cycling - its
// rotation is read for the diagnostics display but not transmitted).
constexpr uint8_t ENCODER_CLK_PIN = 32;
constexpr uint8_t ENCODER_DT_PIN = 33;
constexpr uint8_t ENCODER_BUTTON_PIN = 25;
constexpr uint8_t SERVO_PIN = 19;

// OutputManager channel numbering is local to this board - it identifies
// "which output device" and is decoupled from the wire-level ValueChannel/
// StateSnapshot channel IDs, both of which are decoded first and then
// routed through here.
constexpr uint8_t OUTPUT_CHANNEL_SERVO = 1;
constexpr uint8_t OUTPUT_CHANNEL_LED = 2;

unsigned long cueFlashUntil = 0;
unsigned long lastDisplayRefresh = 0;
// TX's remote encoder position/button state, as last received over radio.
int encoderValue = 0;
int servoAngle = 0;
bool encoderButtonPressed = false;
bool displayReady = false;
// True once any packet has ever arrived - distinguishes "never connected"
// from "was connected, link since dropped" for LinkState/display purposes.
bool hasReceivedValidPacket = false;
LinkState linkState = LinkState::WaitingForLink;
StageLink::StatusPageCycler pageCycler;
StageLink::ReliableRadio radio;
StageLink::OutputManager outputManager;
ServoOutput servoOutputDevice(SERVO_PIN);
LEDOutput ledOutputDevice;

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
            Encoder::position(),
            servoAngle
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

// WaitingForLink (never heard from TX) is deliberately distinct from
// LinkLost (was connected, now quiet) even though both mean "no active
// link" - operators need to tell "never paired" apart from "dropped."
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

    StageLink::ConfigManager::begin();

    StatusLED::begin();
    StatusLED::setOffline();

    Encoder::begin(
        ENCODER_CLK_PIN,
        ENCODER_DT_PIN,
        ENCODER_BUTTON_PIN
    );

    if (!outputManager.registerDevice(OUTPUT_CHANNEL_SERVO, &servoOutputDevice))
    {
        Serial.println("Servo output failed to initialize");
    }

    if (!outputManager.registerDevice(OUTPUT_CHANNEL_LED, &ledOutputDevice))
    {
        Serial.println("LED output failed to initialize");
    }

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
        // First packet ever seen from TX: we have no state yet (encoder,
        // button, servo are all still at their power-on defaults), so ask
        // TX for a full snapshot rather than waiting for the next live change.
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
        // VALUE_UPDATE is the live path: applied immediately as TX's input
        // changes. STATE_SNAPSHOT (below) only fires on reconnect.
        else if (packet.type == StageLink::PacketType::VALUE_UPDATE &&
                 packet.payloadLength == StageLink::VALUE_UPDATE_PAYLOAD_SIZE &&
                 StageLink::decodeValueUpdateChannel(packet.payload) ==
                     StageLink::ValueChannel::Encoder)
        {
            encoderValue = StageLink::decodeValueUpdateValue(packet.payload);

            Serial.print("Encoder received: ");
            Serial.println(encoderValue);

            if (displayReady)
            {
                showCurrentPage();
            }
        }
        else if (packet.type == StageLink::PacketType::VALUE_UPDATE &&
                 packet.payloadLength == StageLink::VALUE_UPDATE_PAYLOAD_SIZE &&
                 StageLink::decodeValueUpdateChannel(packet.payload) ==
                     StageLink::ValueChannel::Servo)
        {
            servoAngle = StageLink::decodeValueUpdateValue(packet.payload);
            outputManager.update(OUTPUT_CHANNEL_SERVO, servoAngle);

            Serial.print("Servo angle received: ");
            Serial.println(servoAngle);

            showCurrentPage();
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
        // Full resync: applied generically by item type, so adding a new
        // StateItemType later (Dmx, StepperPosition, etc.) only needs a
        // new case here, not a new packet type or payload format.
        else if (packet.type == StageLink::PacketType::STATE_SNAPSHOT)
        {
            StageLink::StateSnapshot snapshot;

            if (StageLink::deserializeStateSnapshot(packet.payload, packet.payloadLength, snapshot))
            {
                for (uint8_t i = 0; i < StageLink::stateItemCount(snapshot); ++i)
                {
                    const StageLink::StateItem &item = snapshot.items[i];

                    switch (item.type)
                    {
                        case StageLink::StateItemType::Encoder:
                            encoderValue = item.value;
                            break;
                        case StageLink::StateItemType::Button:
                            encoderButtonPressed = item.value != 0;
                            break;
                        case StageLink::StateItemType::Servo:
                            servoAngle = item.value;
                            outputManager.update(OUTPUT_CHANNEL_SERVO, servoAngle);
                            break;
                        default:
                            break; // unknown/future item types are safely ignored
                    }
                }

                Serial.print("State snapshot received: encoder ");
                Serial.print(encoderValue);
                Serial.print(", button ");
                Serial.print(encoderButtonPressed ? "PRESSED" : "RELEASED");
                Serial.print(", servo ");
                Serial.println(servoAngle);

                showCurrentPage();
            }
            else
            {
                Serial.println("Malformed state snapshot received");
            }
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

    // Briefly override the status LED to acknowledge a received cue,
    // overriding the normal ready/offline color for cueFlashUntil's duration.
    if (millis() < cueFlashUntil)
    {
        StatusLED::setConfig();
        return;
    }

}
