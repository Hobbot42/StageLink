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
#include "EffectStorage.h"
#include "TriggerManager.h"
#include "LEDOutput.h"
#include "DeviceInfo.h"
#include "Label.h"
#include "LabelEditor.h"
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
    UnitLabel = 2,
    EffectTest = 3,
    TriggerStatus = 4
};
constexpr uint8_t RX_PAGE_COUNT = 5;
constexpr unsigned long DISPLAY_REFRESH_INTERVAL_MS = 250;

// Holding the local button this long fires a page-specific action
// instead of cycling to the next page: on the Unit Label page, starts
// editing it; on the Effect Test page, fires TRIGGER_LOCAL_BUTTON_1 (see
// loop()). Same threshold/pattern TX uses for its own short-press/
// long-press split (see TX main.cpp). While already editing the label,
// the same hold instead steps back a character (see LabelEditor::back())
// - reusing this one control for both jobs since RX's local encoder only
// has a single button.
constexpr unsigned long LOCAL_BUTTON_HOLD_MS = 600;

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

// EffectStorage slot for the one effect this phase deals with - see
// buildTestEffect()/setup(). Internal (0-based, EffectStorage's own
// indexing) - the OLED page below labels it "Effect 1" for the operator.
constexpr uint8_t TEST_EFFECT_ID = 0;

unsigned long cueFlashUntil = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long lastOutputRefresh = 0;
unsigned long localButtonPressStartTime = 0;
// True once the current hold has already fired an action (opening edit
// mode, stepping back a character, or firing the effect test trigger) -
// keeps the eventual release from double-triggering (see loop()) and
// distinguishes "this release ended a hold that already did something"
// from "this release is a normal confirm/cycle press."
bool localButtonHoldTriggered = false;

// Unit Label edit UI - see beginUnitLabelEdit()/commitUnitLabelEdit()
// below. unitLabelEditor holds the in-progress value; it's only copied
// into localDeviceInfo.unitLabel (and persisted) once every character
// has been confirmed, so a cancelled/interrupted edit never corrupts the
// last saved label. Generic across any future label type - see
// StageLink_Common/src/LabelEditor.h.
StageLink::LabelEditor unitLabelEditor;
// TX's remote encoder position/button state, as last received over radio.
int encoderValue = 0;
int servoAngle = 0;
bool encoderButtonPressed = false;
bool displayReady = false;
// Set once in setup() - whether the test effect came from EffectStorage
// (a previous save survived reset/power cycle) or had to be generated
// fresh from buildTestEffect() because nothing was stored yet. Purely
// informational, for the Effect Test OLED page (see showCurrentPage()).
bool effectLoadedFromStorage = false;
// Updated by fireTrigger() whenever a trigger actually fires - purely
// informational, for the Trigger Status OLED page. "None"/0 until the
// first trigger fires after boot.
const char *lastTriggerLabel = "None";
int lastTriggerEffectNumber = 0;
// True once any packet has ever arrived - distinguishes "never connected"
// from "was connected, link since dropped" for LinkState/display purposes.
bool hasReceivedValidPacket = false;
LinkState linkState = LinkState::WaitingForLink;
StageLink::StatusPageCycler pageCycler;
StageLink::ReliableRadio radio;
StageLink::OutputManager outputManager;
StageLink::EffectEngine effectEngine;
StageLink::TriggerManager triggerManager;
ServoOutput servoOutputDevice(SERVO_PIN);
LEDOutput ledOutputDevice;
LEDChannelProxy ledRedProxy(ledOutputDevice, LEDChannelProxy::Channel::Red);
LEDChannelProxy ledGreenProxy(ledOutputDevice, LEDChannelProxy::Channel::Green);
LEDChannelProxy ledBlueProxy(ledOutputDevice, LEDChannelProxy::Channel::Blue);

// Default content for TEST_EFFECT_ID, used only the first time RX ever
// boots with nothing saved under it yet (see setup()) - once saved via
// EffectStorage, the persisted copy is what actually loads on every
// later boot, this function doesn't run again. Exercises two real
// output devices (LED, servo) through OutputManager exactly like any
// other channel update, to prove the playback engine actually drives
// hardware and not just internal state. Triggered via the existing Cue
// packet (RX main loop()'s Cue handler) - reusing TX's existing
// physical cue button rather than adding any new trigger mechanism.
void buildTestEffect(StageLink::Effect &effect)
{
    StageLink::clearEffect(effect);
    StageLink::addEffectStep(effect, OUTPUT_CHANNEL_LED_BRIGHTNESS, 255, 500);
    StageLink::addEffectStep(effect, OUTPUT_CHANNEL_SERVO, 90, 1000);
    StageLink::addEffectStep(effect, OUTPUT_CHANNEL_LED_BRIGHTNESS, 0, 0);
}

// Fires triggerID through triggerManager and records it for the Trigger
// Status OLED page - the single call site both the Cue handler and the
// Effect Test page's local-button hold go through, so "what/when did a
// trigger last fire" stays in one place. Caller is still responsible
// for showCurrentPage() afterward if the display should reflect it now
// rather than at the next periodic refresh.
void fireTrigger(const char *label, uint8_t triggerID)
{
    triggerManager.trigger(triggerID);

    lastTriggerLabel = label;

    uint8_t assignedEffectID = triggerManager.getAssignedEffect(triggerID);
    lastTriggerEffectNumber = (assignedEffectID == StageLink::TriggerManager::NO_EFFECT_ASSIGNED)
        ? 0
        : (assignedEffectID + 1);
}

// Built once in setup() (see initDeviceInfo()) and sent as-is whenever
// TX asks - see CAPABILITY_REQUEST handling in loop().
StageLink::DeviceInfo localDeviceInfo;

// Restores the persisted unit label into localDeviceInfo.unitLabel -
// stored as one raw character code per NVS key (ConfigManager has no
// string type), defaulting to LABEL_DEFAULT on a board that's never had
// one saved.
void loadUnitLabel()
{
    char restored[StageLink::LABEL_BUFFER_SIZE];

    for (uint8_t i = 0; i < StageLink::LABEL_MAX_LENGTH; ++i)
    {
        char key[16];
        snprintf(key, sizeof(key), "unitLabelC%u", i);

        uint8_t defaultValue = static_cast<uint8_t>(
            i < strlen(StageLink::LABEL_DEFAULT) ? StageLink::LABEL_DEFAULT[i] : ' '
        );
        restored[i] = static_cast<char>(StageLink::ConfigManager::getUInt8(key, defaultValue));
    }
    restored[StageLink::LABEL_MAX_LENGTH] = '\0';
    StageLink::trimTrailingLabelSpaces(restored);

    StageLink::setLabelText(localDeviceInfo.unitLabel, StageLink::LABEL_BUFFER_SIZE, restored);
}

void saveUnitLabel()
{
    for (uint8_t i = 0; i < StageLink::LABEL_MAX_LENGTH; ++i)
    {
        char key[16];
        snprintf(key, sizeof(key), "unitLabelC%u", i);

        char c = i < strlen(localDeviceInfo.unitLabel) ? localDeviceInfo.unitLabel[i] : ' ';
        StageLink::ConfigManager::putUInt8(key, static_cast<uint8_t>(c));
    }
}

// Enters edit mode starting from the currently saved label, not a blank
// slate - rotating from "where it already is" rather than resetting to
// blank every time.
void beginUnitLabelEdit()
{
    unitLabelEditor.begin(localDeviceInfo.unitLabel);
    Serial.println("Unit label edit: character 1");
}

// Called once unitLabelEditor.confirmChar() reports the last character
// was just confirmed - commits the trimmed result to localDeviceInfo and
// ConfigManager.
void commitUnitLabelEdit()
{
    char committed[StageLink::LABEL_BUFFER_SIZE];
    StageLink::setLabelText(committed, StageLink::LABEL_BUFFER_SIZE, unitLabelEditor.buffer());
    StageLink::trimTrailingLabelSpaces(committed);

    StageLink::setLabelText(localDeviceInfo.unitLabel, StageLink::LABEL_BUFFER_SIZE, committed);
    saveUnitLabel();

    Serial.print("Unit label saved: ");
    Serial.println(localDeviceInfo.unitLabel);
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

    // User-assigned label, not factory identity - see loadUnitLabel().
    loadUnitLabel();

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
    // selected - see beginUnitLabelEdit()/commitUnitLabelEdit().
    if (unitLabelEditor.isActive())
    {
        Display::showUnitLabelEdit(unitLabelEditor.buffer(), unitLabelEditor.cursor());
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
    else if (pageCycler.page() == static_cast<uint8_t>(RxPage::UnitLabel))
    {
        Display::showUnitLabel(localDeviceInfo.unitLabel);
    }
    else if (pageCycler.page() == static_cast<uint8_t>(RxPage::EffectTest))
    {
        Display::showEffectTest(effectEngine.isRunning(), effectLoadedFromStorage);
    }
    else
    {
        Display::showTriggerStatus(lastTriggerLabel, lastTriggerEffectNumber);
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
    StageLink::EffectStorage::begin();

    StageLink::Effect testEffect;
    effectLoadedFromStorage = StageLink::EffectStorage::loadEffect(TEST_EFFECT_ID, testEffect);

    if (!effectLoadedFromStorage)
    {
        buildTestEffect(testEffect);
        StageLink::EffectStorage::saveEffect(TEST_EFFECT_ID, testEffect);
    }

    effectEngine.loadEffect(testEffect);

    // Hardcoded default assignments for this first version (see
    // TriggerManager.h) - both existing trigger sources point at the
    // one effect that exists so far. A future version can persist/edit
    // these via ConfigManager without changing TriggerManager's API.
    triggerManager.begin(effectEngine);
    triggerManager.assignTrigger(StageLink::TRIGGER_CUE_BUTTON, TEST_EFFECT_ID);
    triggerManager.assignTrigger(StageLink::TRIGGER_LOCAL_BUTTON_1, TEST_EFFECT_ID);

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

    // Local encoder button: short press cycles RX's own OLED page on
    // every page, unconditionally - a long press only does something
    // different on two specific pages (see LOCAL_BUTTON_HOLD_MS below):
    // Unit Label starts editing it, Effect Test fires
    // TRIGGER_LOCAL_BUTTON_1. Either way the hold action fires the
    // moment the hold threshold elapses, while the button is still down,
    // rather than waiting for release, so the display updates as
    // immediate feedback that the hold registered. While editing a unit
    // label, a short press instead confirms the current character and
    // advances (see LabelEditor::confirmChar()), and a hold steps back to
    // re-edit the previous character - or cancels the whole edit at the
    // first character (see LabelEditor::back()). This is purely local UI
    // navigation, distinct from encoderButtonPressed (TX's remote encoder
    // button state, received over radio and unaffected by any of this).
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
            // This release just ended a hold that already did something
            // (opened edit mode, stepped back a character, or fired the
            // effect test trigger) - nothing more to do for it.
            localButtonHoldTriggered = false;
        }
        else if (unitLabelEditor.isActive())
        {
            if (unitLabelEditor.confirmChar())
            {
                commitUnitLabelEdit();
            }
            showCurrentPage();
        }
        else
        {
            pageCycler.next();
            showCurrentPage();
        }
    }

    if (!localButtonHoldTriggered &&
        Encoder::isButtonPressed() &&
        millis() - localButtonPressStartTime >= LOCAL_BUTTON_HOLD_MS)
    {
        if (unitLabelEditor.isActive())
        {
            unitLabelEditor.back();
            if (!unitLabelEditor.isActive())
            {
                Serial.println("Unit label edit cancelled");
            }
            showCurrentPage();
            localButtonHoldTriggered = true;
        }
        else if (pageCycler.page() == static_cast<uint8_t>(RxPage::UnitLabel))
        {
            beginUnitLabelEdit();
            showCurrentPage();
            localButtonHoldTriggered = true;
        }
        else if (pageCycler.page() == static_cast<uint8_t>(RxPage::EffectTest))
        {
            fireTrigger("Local Button", StageLink::TRIGGER_LOCAL_BUTTON_1);
            showCurrentPage();
            localButtonHoldTriggered = true;
        }
    }

    if (unitLabelEditor.isActive())
    {
        int unitLabelTurn = Encoder::consumeTurn();
        if (unitLabelTurn != 0)
        {
            unitLabelEditor.stepChar(unitLabelTurn);
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

            // Routed through TriggerManager rather than calling
            // effectEngine.startEffect() directly - behavior is
            // identical today (TRIGGER_CUE_BUTTON is assigned to the
            // one effect that exists, see setup()) but this is now the
            // same path any future trigger source goes through.
            fireTrigger("Cue", StageLink::TRIGGER_CUE_BUTTON);
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
