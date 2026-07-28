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
#include "GuiController.h"
#include "UiButton.h"
#include "OutputCatalog.h"
#include "ShowStorage.h"
#include "ShowEngine.h"
#include "UpdateMode.h"
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

// Disables TX's old manual test-value path (encoder-driven live
// VALUE_UPDATE and the matching STATE_SNAPSHOT fields) from reaching
// OutputManager, so it can no longer override ShowEngine-commanded
// output state (see StageLink RxQ - Disable Old Remote Output Test
// Control v0.1). The packets themselves are still received, logged, and
// (for servoAngle) reflected on RX's own Status display - only the
// outputManager.update() call is skipped. Flip back to true for
// hardware bring-up/testing without a show loaded.
constexpr bool TX_TEST_OUTPUT_CONTROL_ENABLED = false;

// Seeds ShowEngine with the "Dragon Battle" demo show (see
// ShowEngine::loadTestShow()), but only on a unit with nothing saved -
// saved shows are always left alone. Development convenience only: a
// shipped unit boots with no shows in firmware and the operator creates
// them in Program Mode. Set false to get that behavior.
constexpr bool LOAD_TEST_SHOW_ON_BOOT = true;

// RX's own local encoder (button only used for local page cycling - its
// rotation is read for the diagnostics display but not transmitted).
constexpr uint8_t ENCODER_CLK_PIN = 32;
constexpr uint8_t ENCODER_DT_PIN = 33;
constexpr uint8_t ENCODER_BUTTON_PIN = 26;
constexpr uint8_t SERVO_PIN = 16; // OUT-02 Pin 1

// Dedicated GUI buttons (see GuiController.h) - additional to, not
// replacing, the encoder's own press/hold gestures. Only read while
// guiActive (see loop()); legacy mode's behavior is unaffected.
constexpr uint8_t BACK_BUTTON_PIN = 27;
constexpr uint8_t ACTION_BUTTON_PIN = 4;

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

// FxQ GUI Architecture Prototype v0.1 - see GuiController.h. guiActive
// selects which system currently owns the OLED and local button: true
// (the default, "no menu on boot") routes to guiController; false hands
// control back to the existing pageCycler-driven pages below (Setup >
// Diagnostics enters that mode; holding on the Trigger Status page
// exits it - see loop()). Nothing about the legacy pages themselves
// changes based on this flag, only who gets shown by default.
GuiController guiController;
bool guiActive = true;

// StageLink RxQ ShowEngine foundation (SHOW -> CUE -> ACTION) - see
// ShowEngine.h. Only begin() is called (see setup()); not wired to any
// input or display yet, deliberately - this step only establishes the
// state-tracking layer future work builds on.
ShowEngine showEngine;

// Dedicated Back/Action buttons - see BACK_BUTTON_PIN/ACTION_BUTTON_PIN
// above. Mirror the encoder's hold/press gestures (see loop()) rather
// than replacing them.
UiButton backButton(BACK_BUTTON_PIN);
UiButton actionButton(ACTION_BUTTON_PIN);

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
StageLink::OutputCatalog outputCatalog;
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
    Serial.println("Controller label edit: character 1");
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

    Serial.print("Controller label saved: ");
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
    // GPIO16/17 (SERVO_PIN's connector) are reserved for PSRAM on some
    // ESP32 modules - if PSRAM shows present here and the servo still
    // fails to attach, that's almost certainly why, and SERVO_PIN needs
    // to move to a different OUT connector.
    Serial.print("PSRAM: ");
    Serial.println(ESP.getPsramSize() > 0 ? "present" : "not present");
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

    // Full MAC, formatted for a direct copy-paste into TX main.cpp's
    // receiverAddress[] - TX addresses RX by this array since RX doesn't
    // know TX's address ahead of time (see ReliableRadio::begin()), so
    // this must be updated by hand whenever RX's actual hardware
    // (chip/board) changes.
    Serial.print("MAC (for TX receiverAddress[]): ");
    for (uint8_t i = 0; i < StageLink::DEVICE_ID_LENGTH; ++i)
    {
        if (i > 0)
        {
            Serial.print(", ");
        }
        Serial.print("0x");
        if (localDeviceInfo.deviceId[i] < 0x10)
        {
            Serial.print("0");
        }
        Serial.print(localDeviceInfo.deviceId[i], HEX);
    }
    Serial.println();

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

    if (guiActive)
    {
        guiController.render();
        return;
    }

    // Editing takes over the display regardless of which page is
    // selected - see beginUnitLabelEdit()/commitUnitLabelEdit(). Only
    // reachable in legacy mode (guiActive == false) - the GUI's own
    // Setup > Unit Label flow renders the same shared unitLabelEditor
    // via GuiController::render() instead (see GuiController.cpp).
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

// Reached from GuiController's Setup > Diagnostics (see the callback
// passed to guiController.begin() in setup()) - hands the display and
// local button back to the existing pageCycler-driven pages, starting
// from Status, completely unchanged from how they behaved before this
// GUI prototype existed.
void enterLegacyMode()
{
    guiActive = false;
    pageCycler.begin(RX_PAGE_COUNT);
    showCurrentPage();
}

// Reached by holding on the Trigger Status page (see loop()) - the one
// legacy page with no hold-action of its own already, so this doesn't
// take over a gesture anything else needs.
void exitLegacyMode()
{
    guiActive = true;
    showCurrentPage();
}

// Reached from GuiController's Setup > Update Mode (see UpdateMode.h) -
// brings up WiFi + ArduinoOTA for a firmware push. Deliberately doesn't
// stop radio/ShowEngine/ActionEngine first: ESP-NOW keeps running but
// the TX link will drop the moment WiFi.begin() moves this board's
// radio onto the AP's channel (see the ReliableRadio channel
// discussion) - expected, since the only way out of Update Mode is
// ESP.restart() (see UpdateMode::update()), which re-initializes
// everything cleanly from setup() again.
void enterUpdateMode()
{
    UpdateMode::begin();
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

    // RX's encoder produces 4 quadrature transitions per physical click
    // (confirmed - differs from TX's encoder, which needs 2; see
    // Encoder.h).
    Encoder::begin(
        ENCODER_CLK_PIN,
        ENCODER_DT_PIN,
        ENCODER_BUTTON_PIN,
        4
    );

    backButton.begin();
    actionButton.begin();

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

    // The outputs a cue can program, declared here beside the devices they
    // drive so the wiring is stated once (see OutputCatalog.h). Channel
    // order matters: it is the order that output type's value fields
    // expect, which for an LED is red, green, blue, brightness.
    outputCatalog.begin();

    {
        constexpr uint8_t servoChannels[] = { OUTPUT_CHANNEL_SERVO };
        outputCatalog.addOutput(
            StageLink::OutputCatalog::Type::Servo, servoChannels, 1, "Servo"
        );

        constexpr uint8_t ledChannels[] = {
            OUTPUT_CHANNEL_LED_RED,
            OUTPUT_CHANNEL_LED_GREEN,
            OUTPUT_CHANNEL_LED_BLUE,
            OUTPUT_CHANNEL_LED_BRIGHTNESS,
        };
        outputCatalog.addOutput(
            StageLink::OutputCatalog::Type::Led, ledChannels, 4, "LED"
        );
    }

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

    // ShowEngine foundation - see ShowEngine.h. begin() loads the
    // hardcoded test show and logs it. Called before guiController.begin()
    // below so Show Mode's first render already has real cue data to
    // read, not just a stored (but not yet populated) reference.
    // begin() restores whatever was saved to flash. The demo show only
    // seeds a unit that has nothing stored - without that check it would
    // wipe the operator's saved shows on every boot.
    StageLink::ShowStorage::begin();
    showEngine.begin();

    if (LOAD_TEST_SHOW_ON_BOOT && showEngine.getShowCount() == 0)
    {
        showEngine.loadTestShow();
    }

    // FxQ GUI Architecture Prototype v0.1 - see GuiController.h. Reuses
    // localDeviceInfo/unitLabelEditor/radio/outputManager and the
    // commit/legacy-mode functions already defined above rather than
    // duplicating any of that logic. showEngine feeds Show Mode's
    // display and, via outputManager, GO's action execution - see
    // GuiController.h.
    guiController.begin(
        localDeviceInfo, unitLabelEditor, radio, outputManager, showEngine, outputCatalog,
        commitUnitLabelEdit, enterLegacyMode, enterUpdateMode
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
    // Update Mode owns the board entirely while active (see UpdateMode.h)
    // - normal ESP-NOW/ShowEngine/output processing is skipped outright
    // rather than interleaved, since the two are mutually exclusive for
    // this boot cycle (see enterUpdateMode() above).
    if (UpdateMode::isActive())
    {
        // Back is the way out. Leaving goes through a reboot for the same
        // reason entering does (see UpdateMode.h) - WiFi and ESP-NOW are
        // mutually exclusive for a boot cycle, so there is no tearing one
        // down in place. Anything unsaved is written first.
        backButton.update();
        if (backButton.consumePress())
        {
            if (showEngine.hasUnsavedChanges())
            {
                showEngine.save();
            }

            ESP.restart();
        }

        UpdateMode::update();
        return;
    }

    radio.update();
    Encoder::update();
    backButton.update();
    actionButton.update();
    ledOutputDevice.tick();
    servoOutputDevice.tick();

    // Writes any programming changes to flash once editing has paused -
    // see ShowEngine::tick(). Deliberately after the Update Mode early
    // return above: an OTA reboot shouldn't race a save.
    showEngine.tick(outputManager);
    effectEngine.update();

    // Local encoder button: rotate/press, dispatched to whichever system
    // currently owns the display (see guiActive above). GUI mode uses
    // rotate=navigate, press=select/confirm/GO (see GuiController.h) -
    // the encoder's own hold gesture does nothing in GUI mode anymore,
    // now that the dedicated Back button exists to drive handleBack().
    // Legacy mode keeps its original,
    // completely unchanged page-cycling/Unit-Label-editing/Effect-Test-
    // triggering behavior, hold included. This is purely local UI
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
    bool localButtonReleased = false;
    if (Encoder::consumeButtonStateChange(localButtonPressed))
    {
        if (localButtonPressed)
        {
            localButtonPressStartTime = millis();
            localButtonHoldTriggered = false;
        }
        else
        {
            localButtonReleased = true;
        }
    }

    bool localButtonHoldNow = !localButtonHoldTriggered &&
        Encoder::isButtonPressed() &&
        millis() - localButtonPressStartTime >= LOCAL_BUTTON_HOLD_MS;

    if (guiActive)
    {
        // Holding confirm is its own gesture (see
        // GuiController::handleHoldConfirm()) - it fires while the button
        // is still down, and localButtonHoldTriggered then suppresses the
        // release so one gesture can't run two actions.
        if (localButtonHoldNow)
        {
            localButtonHoldTriggered = true;
            guiController.handleHoldConfirm();
            showCurrentPage();
        }

        if (localButtonReleased && !localButtonHoldTriggered)
        {
            guiController.handlePress();
            showCurrentPage();
        }

        int guiTurn = Encoder::consumeTurn();
        if (guiTurn != 0)
        {
            guiController.handleRotate(guiTurn);
            showCurrentPage();
        }

        // Dedicated Back button: the only way to fire handleBack() in
        // GUI mode now - see GuiController.h. Goes up to the Mode Menu
        // from a root screen (Show Run/Show List/Setup List) and does
        // nothing from the Mode Menu itself.
        if (backButton.consumePress())
        {
            guiController.handleBack();
            showCurrentPage();
        }

        // The Action button mirrors the encoder: a tap confirms, a hold
        // does the hold gesture. UiButton reports a tap on release, so a
        // press that became a hold never also reports as a tap.
        if (actionButton.consumeHold())
        {
            guiController.handleHoldConfirm();
            showCurrentPage();
        }

        if (actionButton.consumePress())
        {
            guiController.handlePress();
            showCurrentPage();
        }
    }
    else
    {
        // Exact legacy behavior - unaffected by any of the above.
        if (localButtonReleased)
        {
            if (localButtonHoldTriggered)
            {
                // This release just ended a hold that already did
                // something (opened edit mode, stepped back a
                // character, fired the effect test trigger, or exited
                // legacy mode) - nothing more to do for it.
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

        if (localButtonHoldNow)
        {
            if (unitLabelEditor.isActive())
            {
                unitLabelEditor.back();
                if (!unitLabelEditor.isActive())
                {
                    Serial.println("Controller label edit cancelled");
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
            else if (pageCycler.page() == static_cast<uint8_t>(RxPage::TriggerStatus))
            {
                // The one legacy page with no hold-action of its own -
                // free to use as the way back to the GUI prototype.
                exitLegacyMode();
                localButtonHoldTriggered = true;
            }
        }

        // Dedicated Back button: a consistent "back/cancel" exit from
        // legacy mode, available from every legacy page - not just
        // TriggerStatus (the hold-based exit above is kept for
        // compatibility, but it's easy to get stuck behind if you don't
        // know which page to reach first, e.g. entering on Diagnostics).
        // Cancels an in-progress label edit first if one is active,
        // matching how the encoder hold already behaves while editing,
        // rather than abandoning the edit and exiting in one step.
        if (backButton.consumePress())
        {
            if (unitLabelEditor.isActive())
            {
                unitLabelEditor.back();
                if (!unitLabelEditor.isActive())
                {
                    Serial.println("Controller label edit cancelled");
                }
            }
            else
            {
                exitLegacyMode();
            }
            showCurrentPage();
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
            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
            {
                outputManager.update(OUTPUT_CHANNEL_SERVO, servoAngle);
            }

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
            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
            {
                outputManager.update(OUTPUT_CHANNEL_LED_RED, value);
            }

            Serial.print("LED red received: ");
            Serial.println(value);
        }
        else if (packet.type == StageLink::PacketType::VALUE_UPDATE &&
                 packet.payloadLength == StageLink::VALUE_UPDATE_PAYLOAD_SIZE &&
                 StageLink::decodeValueUpdateChannel(packet.payload) ==
                     StageLink::ValueChannel::LedGreen)
        {
            int value = StageLink::decodeValueUpdateValue(packet.payload);
            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
            {
                outputManager.update(OUTPUT_CHANNEL_LED_GREEN, value);
            }

            Serial.print("LED green received: ");
            Serial.println(value);
        }
        else if (packet.type == StageLink::PacketType::VALUE_UPDATE &&
                 packet.payloadLength == StageLink::VALUE_UPDATE_PAYLOAD_SIZE &&
                 StageLink::decodeValueUpdateChannel(packet.payload) ==
                     StageLink::ValueChannel::LedBlue)
        {
            int value = StageLink::decodeValueUpdateValue(packet.payload);
            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
            {
                outputManager.update(OUTPUT_CHANNEL_LED_BLUE, value);
            }

            Serial.print("LED blue received: ");
            Serial.println(value);
        }
        else if (packet.type == StageLink::PacketType::VALUE_UPDATE &&
                 packet.payloadLength == StageLink::VALUE_UPDATE_PAYLOAD_SIZE &&
                 StageLink::decodeValueUpdateChannel(packet.payload) ==
                     StageLink::ValueChannel::LedBrightness)
        {
            int value = StageLink::decodeValueUpdateValue(packet.payload);
            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
            {
                outputManager.update(OUTPUT_CHANNEL_LED_BRIGHTNESS, value);
            }

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
                            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
                            {
                                outputManager.update(OUTPUT_CHANNEL_SERVO, servoAngle);
                            }
                            break;
                        case StageLink::StateItemType::LedRed:
                            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
                            {
                                outputManager.update(OUTPUT_CHANNEL_LED_RED, item.value);
                            }
                            break;
                        case StageLink::StateItemType::LedGreen:
                            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
                            {
                                outputManager.update(OUTPUT_CHANNEL_LED_GREEN, item.value);
                            }
                            break;
                        case StageLink::StateItemType::LedBlue:
                            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
                            {
                                outputManager.update(OUTPUT_CHANNEL_LED_BLUE, item.value);
                            }
                            break;
                        case StageLink::StateItemType::LedBrightness:
                            if (TX_TEST_OUTPUT_CONTROL_ENABLED)
                            {
                                outputManager.update(OUTPUT_CHANNEL_LED_BRIGHTNESS, item.value);
                            }
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
