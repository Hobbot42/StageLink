// FxQ
// Product: RxQ
// Version: v0.9.0
//
// Project information is maintained in FxQInfo.h
//
// RxQ (receiver / output unit)
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
//   TX -> RX  CAPABILITY_REQUEST   "report your device info/capabilities"
//   RX -> TX  CAPABILITY_RESPONSE  reply, see DeviceInfo.h
//   TX -> RX  OUTPUT_LIST_REQUEST  "enumerate your output instances"
//   RX -> TX  OUTPUT_LIST_RESPONSE reply, see OutputList.h
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
#include "DeviceInfo.h"
#include "OutputList.h"
#include "RxQInfo.h"
#include "FxQBuildInfo.h"

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
constexpr uint8_t OUTPUT_CHANNEL_LED_BRIGHTNESS = 2;
constexpr uint8_t OUTPUT_CHANNEL_LED_RED = 3;
constexpr uint8_t OUTPUT_CHANNEL_LED_GREEN = 4;
constexpr uint8_t OUTPUT_CHANNEL_LED_BLUE = 5;

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
LEDChannelProxy ledRedProxy(ledOutputDevice, LEDChannelProxy::Channel::Red);
LEDChannelProxy ledGreenProxy(ledOutputDevice, LEDChannelProxy::Channel::Green);
LEDChannelProxy ledBlueProxy(ledOutputDevice, LEDChannelProxy::Channel::Blue);

// Built once in setup() (see initDeviceInfo()) and sent as-is whenever
// TX asks - see CAPABILITY_REQUEST handling in loop().
StageLink::DeviceInfo localDeviceInfo;

void initDeviceInfo()
{
    StageLink::setDeviceInfoField(
        localDeviceInfo.name,
        StageLink::DEVICE_NAME_MAX_LENGTH,
        "RxQ Universal"
    );
    StageLink::setDeviceInfoField(
        localDeviceInfo.firmwareVersion,
        StageLink::DEVICE_VERSION_MAX_LENGTH,
        FxQ::PRODUCT_VERSION
    );
    StageLink::setDeviceInfoField(
        localDeviceInfo.hardwareVersion,
        StageLink::DEVICE_HW_VERSION_MAX_LENGTH,
        FxQ::HARDWARE_REVISION
    );

    // Reflects what's actually registered with OutputManager above -
    // Motion (servo) and LED (addressable strip). Relay/Input aren't
    // implemented on this board, so they're left unset rather than
    // claimed.
    localDeviceInfo.capabilities =
        StageLink::capabilityBit(StageLink::DeviceCapability::Motion) |
        StageLink::capabilityBit(StageLink::DeviceCapability::LedAddressable);
}

void sendCapabilityResponse()
{
    uint8_t payload[StageLink::DEVICE_INFO_WIRE_SIZE];
    uint8_t length = StageLink::serializeDeviceInfo(localDeviceInfo, payload);

    if (radio.send(StageLink::PacketType::CAPABILITY_RESPONSE, payload, length))
    {
        Serial.println("Sent capability response");
    }
    else
    {
        Serial.println("Capability response queue full");
    }
}

// Built once in setup() (see initOutputList()) and sent as-is whenever
// TX asks - see OUTPUT_LIST_REQUEST handling in loop().
StageLink::OutputList localOutputList;

// One descriptor per actual output *instance*, not per OutputManager
// channel registration - e.g. the LED strip has 4 channels (brightness/
// R/G/B) feeding one physical LedAddressable output, so it's still just
// one descriptor here, index 0. This mirrors initDeviceInfo()'s
// capability bitmask: both are built from what main.cpp actually
// instantiated and registered, not introspected from OutputManager
// (which only tracks channel->device bindings, not "how many distinct
// physical devices" - that distinction lives here in application code).
void initOutputList()
{
    StageLink::clearOutputList(localOutputList);
    StageLink::addOutputDescriptor(localOutputList, StageLink::DeviceCapability::Motion, 0);
    StageLink::addOutputDescriptor(localOutputList, StageLink::DeviceCapability::LedAddressable, 0);
}

void sendOutputListResponse()
{
    uint8_t payload[StageLink::MAX_OUTPUT_LIST_PAYLOAD_SIZE];
    uint8_t length = StageLink::serializeOutputList(localOutputList, payload);

    if (radio.send(StageLink::PacketType::OUTPUT_LIST_RESPONSE, payload, length))
    {
        Serial.println("Sent output list response");
    }
    else
    {
        Serial.println("Output list response queue full");
    }
}

void printStartupBanner()
{
    Serial.println();
    Serial.println("========================");
    Serial.println(FxQ::PROJECT_BRAND);
    Serial.println();
    Serial.print("Product: ");
    Serial.println(FxQ::PRODUCT_NAME);
    Serial.print("Version: ");
    Serial.println(FxQ::PRODUCT_VERSION);
    Serial.print("Build: B");
    Serial.println(FXQ_BUILD_NUMBER);
    Serial.print("HW: ");
    Serial.println(FxQ::HARDWARE_REVISION);
    Serial.print("FxQ Link: ");
    Serial.println(FxQ::LINK_VERSION);
    Serial.print("Built: ");
    Serial.print(FXQ_BUILD_DATE);
    Serial.print(" ");
    Serial.println(FXQ_BUILD_TIME);
    Serial.print("Git: ");
    Serial.println(FXQ_GIT_COMMIT);
    Serial.println();
    Serial.println("Starting...");
    Serial.println("========================");
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

    if (!outputManager.registerDevice(OUTPUT_CHANNEL_LED_BRIGHTNESS, &ledOutputDevice))
    {
        Serial.println("LED output failed to initialize");
    }

    outputManager.registerDevice(OUTPUT_CHANNEL_LED_RED, &ledRedProxy);
    outputManager.registerDevice(OUTPUT_CHANNEL_LED_GREEN, &ledGreenProxy);
    outputManager.registerDevice(OUTPUT_CHANNEL_LED_BLUE, &ledBlueProxy);

    initDeviceInfo();
    initOutputList();

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

    printStartupBanner();

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
    ledOutputDevice.tick();

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
        else if (packet.type == StageLink::PacketType::CAPABILITY_REQUEST)
        {
            Serial.println("Capability request received");
            sendCapabilityResponse();
        }
        else if (packet.type == StageLink::PacketType::OUTPUT_LIST_REQUEST)
        {
            Serial.println("Output list request received");
            sendOutputListResponse();
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
        else if (packet.type == StageLink::PacketType::VALUE_UPDATE &&
                 packet.payloadLength == StageLink::VALUE_UPDATE_PAYLOAD_SIZE &&
                 StageLink::decodeValueUpdateChannel(packet.payload) ==
                     StageLink::ValueChannel::LedRed)
        {
            int value = StageLink::decodeValueUpdateValue(packet.payload);
            outputManager.update(OUTPUT_CHANNEL_LED_RED, value);

            Serial.print("LED red received: ");
            Serial.println(value);
        }
        else if (packet.type == StageLink::PacketType::VALUE_UPDATE &&
                 packet.payloadLength == StageLink::VALUE_UPDATE_PAYLOAD_SIZE &&
                 StageLink::decodeValueUpdateChannel(packet.payload) ==
                     StageLink::ValueChannel::LedGreen)
        {
            int value = StageLink::decodeValueUpdateValue(packet.payload);
            outputManager.update(OUTPUT_CHANNEL_LED_GREEN, value);

            Serial.print("LED green received: ");
            Serial.println(value);
        }
        else if (packet.type == StageLink::PacketType::VALUE_UPDATE &&
                 packet.payloadLength == StageLink::VALUE_UPDATE_PAYLOAD_SIZE &&
                 StageLink::decodeValueUpdateChannel(packet.payload) ==
                     StageLink::ValueChannel::LedBlue)
        {
            int value = StageLink::decodeValueUpdateValue(packet.payload);
            outputManager.update(OUTPUT_CHANNEL_LED_BLUE, value);

            Serial.print("LED blue received: ");
            Serial.println(value);
        }
        else if (packet.type == StageLink::PacketType::VALUE_UPDATE &&
                 packet.payloadLength == StageLink::VALUE_UPDATE_PAYLOAD_SIZE &&
                 StageLink::decodeValueUpdateChannel(packet.payload) ==
                     StageLink::ValueChannel::LedBrightness)
        {
            int value = StageLink::decodeValueUpdateValue(packet.payload);
            outputManager.update(OUTPUT_CHANNEL_LED_BRIGHTNESS, value);

            Serial.print("LED brightness received: ");
            Serial.println(value);
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
                        case StageLink::StateItemType::LedRed:
                            outputManager.update(OUTPUT_CHANNEL_LED_RED, item.value);
                            break;
                        case StageLink::StateItemType::LedGreen:
                            outputManager.update(OUTPUT_CHANNEL_LED_GREEN, item.value);
                            break;
                        case StageLink::StateItemType::LedBlue:
                            outputManager.update(OUTPUT_CHANNEL_LED_BLUE, item.value);
                            break;
                        case StageLink::StateItemType::LedBrightness:
                            outputManager.update(OUTPUT_CHANNEL_LED_BRIGHTNESS, item.value);
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
