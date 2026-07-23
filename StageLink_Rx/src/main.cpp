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
#include <WiFi.h>
#include "Display.h"
#include "ReliableRadio.h"
#include "StatusLED.h"
#include "StatusPageCycler.h"
#include "Encoder.h"
#include "ServoOutput.h"
#include "StateSnapshot.h"
#include "ConfigManager.h"
#include "OutputManager.h"
#include "EffectEngine.h"
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
    Diagnostics = 1,
    UnitName = 2,
    EffectTest = 3
};
constexpr uint8_t RX_PAGE_COUNT = 4;
constexpr unsigned long DISPLAY_REFRESH_INTERVAL_MS = 250;

// Holding the local button this long on the Unit Name page starts
// editing instead of cycling to the next page - same threshold/pattern
// TX uses for its own short-press/long-press split (see TX main.cpp).
constexpr unsigned long UNIT_NAME_EDIT_HOLD_MS = 600;

// Periodic hardware-state recovery, not part of the update path - see
// OutputManager::refreshAll()/OutputDevice::refreshState(). Reapplies
// each output's already-known current value so one that lost power
// independently of RX (e.g. an LED strip unplugged and replugged) comes
// back correct on its own, without needing a new radio packet.
constexpr unsigned long OUTPUT_REFRESH_INTERVAL_MS = 750;

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
unsigned long lastOutputRefresh = 0;
unsigned long localButtonPressStartTime = 0;
// True once the current hold has already fired beginUnitNameEdit() -
// keeps the eventual release from double-triggering (see loop()) and
// distinguishes "this release ended the hold that opened edit mode"
// from "this release is a normal confirm/cycle press."
bool localButtonHoldTriggered = false;

// Unit Name edit UI - see beginUnitNameEdit()/confirmUnitNameEditChar()/
// stepUnitNameEditChar() below. unitEditBuffer holds the in-progress
// value; it's only copied into localDeviceInfo.unitName (and persisted)
// once both characters are confirmed, so a cancelled/interrupted edit
// never corrupts the last saved name.
enum class UnitEditState : uint8_t
{
    None,
    EditingChar0,
    EditingChar1
};
UnitEditState unitEditState = UnitEditState::None;
char unitEditBuffer[StageLink::UNIT_NAME_LENGTH] = {'A', '1', '\0'};
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
StageLink::EffectEngine effectEngine;
ServoOutput servoOutputDevice(SERVO_PIN);
LEDOutput ledOutputDevice;
LEDChannelProxy ledRedProxy(ledOutputDevice, LEDChannelProxy::Channel::Red);
LEDChannelProxy ledGreenProxy(ledOutputDevice, LEDChannelProxy::Channel::Green);
LEDChannelProxy ledBlueProxy(ledOutputDevice, LEDChannelProxy::Channel::Blue);

// Hardcoded proof-of-concept effect only - see EffectEngine.h for what's
// deliberately not built yet (saved/multiple effects, triggers, timers,
// wireless RUN_EFFECT, TxQ editing). Exercises two real output devices
// (LED, servo) through OutputManager exactly like any other channel
// update, to prove the playback engine actually drives hardware and not
// just internal state. Triggered via the existing Cue packet (RX main
// loop()'s Cue handler) - reusing TX's existing physical cue button
// rather than adding any new trigger mechanism.
void buildTestEffect(StageLink::Effect &effect)
{
    StageLink::clearEffect(effect);
    StageLink::addEffectStep(effect, OUTPUT_CHANNEL_LED_BRIGHTNESS, 255, 500);
    StageLink::addEffectStep(effect, OUTPUT_CHANNEL_SERVO, 90, 1000);
    StageLink::addEffectStep(effect, OUTPUT_CHANNEL_LED_BRIGHTNESS, 0, 0);
}

// Built once in setup() (see initDeviceInfo()) and sent as-is whenever
// TX asks - see CAPABILITY_REQUEST handling in loop().
StageLink::DeviceInfo localDeviceInfo;

// Restores the persisted unit name into localDeviceInfo.unitName -
// stored as two raw character codes (ConfigManager has no string type),
// defaulting to UNIT_NAME_DEFAULT on a board that's never had one saved.
void loadUnitName()
{
    char restored[StageLink::UNIT_NAME_LENGTH];
    restored[0] = static_cast<char>(StageLink::ConfigManager::getUInt8(
        "unitNameChar0", static_cast<uint8_t>(StageLink::UNIT_NAME_DEFAULT[0])));
    restored[1] = static_cast<char>(StageLink::ConfigManager::getUInt8(
        "unitNameChar1", static_cast<uint8_t>(StageLink::UNIT_NAME_DEFAULT[1])));
    restored[2] = '\0';

    StageLink::setDeviceInfoField(localDeviceInfo.unitName, StageLink::UNIT_NAME_LENGTH, restored);
}

void saveUnitName()
{
    StageLink::ConfigManager::putUInt8("unitNameChar0", static_cast<uint8_t>(localDeviceInfo.unitName[0]));
    StageLink::ConfigManager::putUInt8("unitNameChar1", static_cast<uint8_t>(localDeviceInfo.unitName[1]));
}

uint8_t unitNameCharsetIndexOf(char c)
{
    const char *pos = strchr(StageLink::UNIT_NAME_CHARSET, c);
    return pos != nullptr ? static_cast<uint8_t>(pos - StageLink::UNIT_NAME_CHARSET) : 0;
}

// Enters edit mode starting from the currently saved name, not a blank
// slate - rotating from "where it already is" rather than resetting to
// 'A' every time.
void beginUnitNameEdit()
{
    unitEditBuffer[0] = localDeviceInfo.unitName[0];
    unitEditBuffer[1] = localDeviceInfo.unitName[1];
    unitEditBuffer[2] = '\0';
    unitEditState = UnitEditState::EditingChar0;

    Serial.println("Unit name edit: character 1 of 2");
}

void stepUnitNameEditChar(int direction)
{
    uint8_t slot = unitEditState == UnitEditState::EditingChar0 ? 0 : 1;
    uint8_t index = unitNameCharsetIndexOf(unitEditBuffer[slot]);

    index = static_cast<uint8_t>(
        (index + StageLink::UNIT_NAME_CHARSET_LENGTH + (direction > 0 ? 1 : -1)) %
        StageLink::UNIT_NAME_CHARSET_LENGTH
    );
    unitEditBuffer[slot] = StageLink::UNIT_NAME_CHARSET[index];
}

// First press confirms character 1 and moves to character 2; second
// press confirms character 2, commits the result to localDeviceInfo and
// ConfigManager, and exits edit mode.
void confirmUnitNameEditChar()
{
    if (unitEditState == UnitEditState::EditingChar0)
    {
        unitEditState = UnitEditState::EditingChar1;
        Serial.println("Unit name edit: character 2 of 2");
        return;
    }

    unitEditState = UnitEditState::None;
    StageLink::setDeviceInfoField(localDeviceInfo.unitName, StageLink::UNIT_NAME_LENGTH, unitEditBuffer);
    saveUnitName();

    Serial.print("Unit name saved: ");
    Serial.println(localDeviceInfo.unitName);
}

void initDeviceInfo()
{
    StageLink::setDeviceInfoField(
        localDeviceInfo.name,
        StageLink::DEVICE_NAME_MAX_LENGTH,
        "RxQ Universal"
    );

    // Permanent, factory-assigned, unique per board - see DeviceInfo.h.
    // Safe to call this early: WiFi.macAddress() reads the eFuse-burned
    // MAC directly when the WiFi driver isn't started yet, it doesn't
    // require radio.begin() to have run first.
    WiFi.macAddress(localDeviceInfo.deviceId);

    // User-assigned label, not factory identity - see loadUnitName().
    loadUnitName();

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

    // Device identity, straight from localDeviceInfo (see initDeviceInfo(),
    // called before this in setup()) - debug/identification only, not a
    // separate source of truth for name/ID/firmware.
    Serial.println(localDeviceInfo.name);
    char idBuffer[StageLink::DEVICE_ID_SHORT_STRING_LENGTH];
    StageLink::formatDeviceIdShort(localDeviceInfo.deviceId, idBuffer, sizeof(idBuffer));
    Serial.print("ID: ");
    Serial.println(idBuffer);
    Serial.print("FW: ");
    Serial.println(localDeviceInfo.firmwareVersion);
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

    // Editing takes over the display regardless of which page is
    // selected - see beginUnitNameEdit()/confirmUnitNameEditChar().
    if (unitEditState != UnitEditState::None)
    {
        Display::showUnitNameEdit(
            unitEditBuffer[0],
            unitEditBuffer[1],
            unitEditState == UnitEditState::EditingChar0 ? 0 : 1
        );
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
    else if (pageCycler.page() == static_cast<uint8_t>(RxPage::Diagnostics))
    {
        StageLink::RadioDiagnostics diagnostics = radio.diagnostics();

        Display::showDiagnostics(
            linkStateText(),
            diagnostics.rssiAvailable,
            diagnostics.rssi,
            diagnostics.packetsReceived,
            diagnostics.totalRetries + diagnostics.packetsFailed,
            localDeviceInfo.deviceId
        );
    }
    else if (pageCycler.page() == static_cast<uint8_t>(RxPage::UnitName))
    {
        Display::showUnitName(localDeviceInfo.unitName);
    }
    else
    {
        Display::showEffectTest(effectEngine.isRunning());
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

    effectEngine.begin(outputManager);
    StageLink::Effect testEffect;
    buildTestEffect(testEffect);
    effectEngine.loadEffect(testEffect);

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
    effectEngine.update();

    // Local encoder button: short press cycles RX's own OLED page; a long
    // press specifically on the Unit Name page starts editing it instead
    // (see UNIT_NAME_EDIT_HOLD_MS) - fires the moment the hold threshold
    // elapses, while the button is still down, rather than waiting for
    // release, so the display switches to the edit view as immediate
    // feedback that the hold registered. While editing, every press
    // confirms the current character and advances rather than cycling
    // pages - see confirmUnitNameEditChar(). This is purely local UI
    // navigation, distinct from encoderButtonPressed (TX's remote
    // encoder button state, received over radio and unaffected by any
    // of this).
    //
    // The state-change edge is consumed first, so a fresh press always
    // resets localButtonPressStartTime before the hold check below ever
    // runs - checking hold duration first would (and did) read the
    // previous press's stale start time on the very iteration a new
    // press begins, firing the hold action instantly on a short click.
    bool localButtonPressed;
    if (Encoder::consumeButtonStateChange(localButtonPressed))
    {
        if (localButtonPressed)
        {
            localButtonPressStartTime = millis();
            localButtonHoldTriggered = false;
        }
        else if (localButtonHoldTriggered)
        {
            // This release just ended the hold that already opened edit
            // mode below - nothing more to do for it.
            localButtonHoldTriggered = false;
        }
        else if (unitEditState != UnitEditState::None)
        {
            confirmUnitNameEditChar();
            showCurrentPage();
        }
        else
        {
            pageCycler.next();
            showCurrentPage();
        }
    }

    if (unitEditState == UnitEditState::None &&
        !localButtonHoldTriggered &&
        Encoder::isButtonPressed() &&
        pageCycler.page() == static_cast<uint8_t>(RxPage::UnitName) &&
        millis() - localButtonPressStartTime >= UNIT_NAME_EDIT_HOLD_MS)
    {
        beginUnitNameEdit();
        showCurrentPage();
        localButtonHoldTriggered = true;
    }

    if (unitEditState != UnitEditState::None)
    {
        int unitEditTurn = Encoder::consumeTurn();
        if (unitEditTurn != 0)
        {
            stepUnitNameEditChar(unitEditTurn);
            showCurrentPage();
        }
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

            // Reuses the existing cue trigger to demonstrate the effect
            // engine on real hardware - no new packet or trigger
            // mechanism, see buildTestEffect().
            effectEngine.startEffect();
            showCurrentPage();
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

    // Recovery only - state changes still apply immediately via
    // outputManager.update() above, this just guards against an output
    // device losing power on its own.
    if (millis() - lastOutputRefresh >= OUTPUT_REFRESH_INTERVAL_MS)
    {
        outputManager.refreshAll();
        lastOutputRefresh = millis();
    }

    // Briefly override the status LED to acknowledge a received cue,
    // overriding the normal ready/offline color for cueFlashUntil's duration.
    if (millis() < cueFlashUntil)
    {
        StatusLED::setConfig();
        return;
    }

}
