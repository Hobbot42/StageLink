// FxQ
// Product: TxQ
// Version: v0.9.0
//
// Project information is maintained in FxQInfo.h
//
// TxQ (handheld control unit)
// Reads the operator's rotary encoder and buttons, and is the source of
// truth for encoder position, button state, and the derived servo angle.
// Sends that state to RX both live (VALUE_UPDATE/BUTTON_EVENT as it
// changes) and as a full resync (STATE_SNAPSHOT) whenever RX asks for
// one via STATE_REQUEST, or after a heartbeat re-establishes the link.
// Belongs to: StageLink_Tx.
//
// Packet flow (see ReliableRadio.h/StageLinkProtocol.h for the transport
// and wire format):
//   TX -> RX  Heartbeat        periodic liveness signal
//   TX -> RX  VALUE_UPDATE     live encoder/servo value change
//   TX -> RX  BUTTON_EVENT     live encoder-button press/release
//   TX -> RX  Cue              momentary cue-button trigger
//   RX -> TX  STATE_REQUEST    "send me your full current state"
//   TX -> RX  STATE_SNAPSHOT   full resync reply (see StateSnapshot.h)
//   TX -> RX  CAPABILITY_REQUEST   "report your device info/capabilities"
//   RX -> TX  CAPABILITY_RESPONSE  reply, see DeviceInfo.h
//   TX -> RX  OUTPUT_LIST_REQUEST  "enumerate your output instances",
//                                   sent once DeviceInfo is in hand
//   RX -> TX  OUTPUT_LIST_RESPONSE reply, see OutputList.h
#include <Arduino.h>
#include "Display.h"
#include "ReliableRadio.h"
#include "StatusLED.h"
#include "Button.h"
#include "Encoder.h"
#include "StatusPageCycler.h"
#include "StateSnapshot.h"
#include "OutputLimiter.h"
#include "ConfigManager.h"
#include "DeviceInfo.h"
#include "OutputList.h"
#include "RemoteDevice.h"
#include "TxQInfo.h"
#include "FxQBuildInfo.h"

namespace
{
uint8_t receiverAddress[] = {
    0x68, 0x09, 0x47, 0xAC, 0xA5, 0x04
};

// Rotary encoder + integrated button pins.
constexpr uint8_t ENCODER_CLK_PIN = 32;
constexpr uint8_t ENCODER_DT_PIN = 33;
constexpr uint8_t ENCODER_BUTTON_PIN = 25;

enum class TxPage : uint8_t
{
    Status = 0,
    Diagnostics = 1,
    DeviceInfo = 2
};
constexpr uint8_t TX_PAGE_COUNT = 3;
constexpr unsigned long DISPLAY_REFRESH_INTERVAL_MS = 250;

// Encoder button: short press cycles control mode, a hold past this
// threshold cycles the OLED page instead (see loop()).
constexpr unsigned long PAGE_CYCLE_HOLD_MS = 600;

// Disables the encoder's live output-control send (sendControlValue()
// below) so it can no longer override RxQ's ShowEngine-commanded output
// state - the old hardware-bring-up test path, now superseded by real
// cues (see StageLink RxQ - Disable Old Remote Output Test Control
// v0.1). Local control-mode cycling/display and sendEncoderValue()
// (informational only - RX doesn't drive any output from it) are
// unaffected. Flip back to true for hardware bring-up/testing without a
// show loaded on RX.
constexpr bool LIVE_OUTPUT_CONTROL_ENABLED = false;

// TX has no physical outputs of its own - the encoder drives whichever
// remote channel is currently selected (see ControlMode), and RX applies
// it to the matching hardware (servo angle, LED channel, ...). Each
// channel's value is accumulated from relative encoder steps (via
// OutputLimiter) rather than recomputed from Encoder::position()
// directly, so it clamps without windup: once a limit is hit, further
// movement in that direction is dropped instead of silently building up,
// and reversing direction responds immediately instead of first having
// to travel back through the clamped range.
enum class ControlMode : uint8_t
{
    Servo = 0,
    LedRed = 1,
    LedGreen = 2,
    LedBlue = 3,
    LedBrightness = 4
};
constexpr uint8_t CONTROL_MODE_COUNT = 5;

// STATE_SNAPSHOT channel IDs (application-level, not part of the wire
// protocol itself - see StateSnapshot.h). Gaps between numbers are
// intentional, leaving room for future channels within each group.
constexpr uint8_t CHANNEL_ENCODER = 1;
constexpr uint8_t CHANNEL_BUTTON = 2;
constexpr uint8_t CHANNEL_SERVO = 10;
constexpr uint8_t CHANNEL_LED_RED = 11;
constexpr uint8_t CHANNEL_LED_GREEN = 12;
constexpr uint8_t CHANNEL_LED_BLUE = 13;
constexpr uint8_t CHANNEL_LED_BRIGHTNESS = 14;

struct ControlChannelConfig
{
    int minValue;
    int maxValue;
    int initialValue;
    int stepPerClick;
    StageLink::ValueChannel wireChannel;
    StageLink::StateItemType snapshotType;
    uint8_t snapshotChannel;
    const char *label;
};

// Indexed by ControlMode. initialValue is the safe startup default used
// whenever there's no saved scene/state to restore (there isn't one yet
// - see ControlChannelConfig's initialValue field) - Servo's own safe
// default (90, centered) predates this and is unchanged; the LED entries
// were previously seeded bright/white for on-bench visibility, now off
// to match RX's own boot-safe default (see StageLink_Rx/LEDOutput.cpp).
// If saved-state restore is added later, it overrides these, it doesn't
// replace them - initialValue stays the fallback for a device with
// nothing saved yet.
constexpr ControlChannelConfig CONTROL_CHANNELS[CONTROL_MODE_COUNT] =
{
    { 0, 180, 90, 2, StageLink::ValueChannel::Servo, StageLink::StateItemType::Servo, CHANNEL_SERVO, "Servo" },
    { 0, 255, 0, 5, StageLink::ValueChannel::LedRed, StageLink::StateItemType::LedRed, CHANNEL_LED_RED, "Red" },
    { 0, 255, 0, 5, StageLink::ValueChannel::LedGreen, StageLink::StateItemType::LedGreen, CHANNEL_LED_GREEN, "Green" },
    { 0, 255, 0, 5, StageLink::ValueChannel::LedBlue, StageLink::StateItemType::LedBlue, CHANNEL_LED_BLUE, "Blue" },
    { 0, 255, 0, 5, StageLink::ValueChannel::LedBrightness, StageLink::StateItemType::LedBrightness, CHANNEL_LED_BRIGHTNESS, "Bright" }
};

unsigned long lastHeartbeatTime = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long encoderButtonPressStartTime = 0;
// True once the current hold has already fired the page-cycle action -
// keeps the eventual release from also cycling the control mode, and
// distinguishes "this release ended a hold that already acted" from "a
// genuine short press, only resolvable at release."
bool encoderHoldTriggered = false;
bool buttonWasPressed = false;
bool inputStateSnapshotNeeded = true;
bool capabilityRequestNeeded = true;
bool displayReady = false;
StageLink::StatusPageCycler pageCycler;
StageLink::ReliableRadio radio;

// Everything TX has learned about the connected RX - device info, output
// list, and (once the link/discovery status flags say so) whether it's
// ready to build UI/control mapping from. See RemoteDevice.h. Requested
// once DeviceInfo is in hand (not on a heartbeat timer like
// capabilityRequestNeeded - there's nothing to ask for until we know RX
// exists). No OLED page consumes the output list yet, see Serial logging
// on receipt (RemoteDevice::logSummary()).
RemoteDevice remoteDevice;

// One clamped accumulator per control channel, indexed by ControlMode -
// see the ControlMode/ControlChannelConfig comment above for why.
StageLink::LimitedOutput controlOutputs[CONTROL_MODE_COUNT];

// Which channel the encoder currently drives - cycled by the encoder
// button (see loop()).
ControlMode currentMode = ControlMode::Servo;

int currentControlValue()
{
    return controlOutputs[static_cast<uint8_t>(currentMode)].value;
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
            Encoder::isButtonPressed(),
            CONTROL_CHANNELS[static_cast<uint8_t>(currentMode)].label,
            currentControlValue()
        );
    }
    else if (pageCycler.page() == static_cast<uint8_t>(TxPage::Diagnostics))
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
    else
    {
        Display::showDeviceInfo(remoteDevice);
    }
}

// --- Live update senders (VALUE_UPDATE/BUTTON_EVENT) ---
// Fired immediately when local input changes; these are the low-latency
// path. STATE_SNAPSHOT (below) exists only to recover from a dropped
// connection, not for normal operation.

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

void sendControlValue(ControlMode mode)
{
    const ControlChannelConfig &config = CONTROL_CHANNELS[static_cast<uint8_t>(mode)];
    int value = controlOutputs[static_cast<uint8_t>(mode)].value;

    uint8_t valuePayload[StageLink::VALUE_UPDATE_PAYLOAD_SIZE];
    StageLink::encodeValueUpdate(
        config.wireChannel,
        value,
        valuePayload
    );

    if (radio.send(
            StageLink::PacketType::VALUE_UPDATE,
            valuePayload,
            sizeof(valuePayload)
        ))
    {
        Serial.print("Queued ");
        Serial.print(config.label);
        Serial.print(" update: ");
        Serial.println(value);
    }
    else
    {
        Serial.print(config.label);
        Serial.println(" queue full");
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

// Asked once after the link comes up (see inputStateSnapshotNeeded's
// twin, capabilityRequestNeeded, in loop()) - RX answers with a
// CAPABILITY_RESPONSE carrying its DeviceInfo.
void sendCapabilityRequest()
{
    if (radio.send(StageLink::PacketType::CAPABILITY_REQUEST))
    {
        Serial.println("Queued capability request");
    }
    else
    {
        Serial.println("Capability request queue full");
    }
}

// Sent once a CAPABILITY_RESPONSE has been successfully parsed (see
// loop()) - RX answers with an OUTPUT_LIST_RESPONSE enumerating its
// actual output instances.
void sendOutputListRequest()
{
    if (radio.send(StageLink::PacketType::OUTPUT_LIST_REQUEST))
    {
        Serial.println("Queued output list request");
    }
    else
    {
        Serial.println("Output list request queue full");
    }
}

// --- Full state resync (STATE_SNAPSHOT) ---
// Sent in response to RX's STATE_REQUEST, and after a heartbeat proves
// the link is back up following a drop (see inputStateSnapshotNeeded in
// loop()) - this is what lets RX recover its encoder/button/servo state
// without waiting for the next live change on TX.
void sendCurrentInputState()
{
    Serial.println("Sending input state snapshot");

    StageLink::StateSnapshot snapshot;
    StageLink::clearStateSnapshot(snapshot);

    StageLink::addOrUpdateStateItem(
        snapshot,
        CHANNEL_ENCODER,
        StageLink::StateItemType::Encoder,
        Encoder::position()
    );
    StageLink::addOrUpdateStateItem(
        snapshot,
        CHANNEL_BUTTON,
        StageLink::StateItemType::Button,
        Encoder::isButtonPressed() ? 1 : 0
    );
    // All control channels resync regardless of which one the encoder
    // currently drives - RX needs every channel's last known value, not
    // just the active one.
    for (uint8_t i = 0; i < CONTROL_MODE_COUNT; ++i)
    {
        StageLink::addOrUpdateStateItem(
            snapshot,
            CONTROL_CHANNELS[i].snapshotChannel,
            CONTROL_CHANNELS[i].snapshotType,
            controlOutputs[i].value
        );
    }

    uint8_t snapshotPayload[StageLink::MAX_STATE_SNAPSHOT_PAYLOAD_SIZE];
    uint8_t payloadLength = StageLink::serializeStateSnapshot(snapshot, snapshotPayload);

    if (!radio.send(
            StageLink::PacketType::STATE_SNAPSHOT,
            snapshotPayload,
            payloadLength
        ))
    {
        Serial.println("State snapshot queue full");
    }
}
}

void setup()
{
    Serial.begin(115200);

    StageLink::ConfigManager::begin();

    StatusLED::begin();

    Button::begin();

    // TX's encoder produces 2 quadrature transitions per physical click
    // (confirmed - differs from RX's encoder, which needs 4; see
    // Encoder.h).
    Encoder::begin(
        ENCODER_CLK_PIN,
        ENCODER_DT_PIN,
        ENCODER_BUTTON_PIN,
        2
    );

    for (uint8_t i = 0; i < CONTROL_MODE_COUNT; ++i)
    {
        StageLink::initLimitedOutput(
            controlOutputs[i],
            CONTROL_CHANNELS[i].initialValue,
            CONTROL_CHANNELS[i].minValue,
            CONTROL_CHANNELS[i].maxValue
        );
    }

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

    printStartupBanner();

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

    // RX asks for this after it (re)connects and doesn't yet have TX's state.
    StageLink::Packet incomingPacket;
    while (radio.receive(incomingPacket))
    {
        if (incomingPacket.type == StageLink::PacketType::STATE_REQUEST)
        {
            Serial.println("State request received");
            sendCurrentInputState();
        }
        else if (incomingPacket.type == StageLink::PacketType::CAPABILITY_RESPONSE)
        {
            StageLink::DeviceInfo info;

            if (StageLink::deserializeDeviceInfo(
                    incomingPacket.payload,
                    incomingPacket.payloadLength,
                    info
                ))
            {
                remoteDevice.setDeviceInfo(info);

                Serial.print("Capability response received: ");
                Serial.println(remoteDevice.info.name);

                showCurrentPage();
                sendOutputListRequest();
            }
            else
            {
                Serial.println("Malformed capability response received");
            }
        }
        else if (incomingPacket.type == StageLink::PacketType::OUTPUT_LIST_RESPONSE)
        {
            StageLink::OutputList list;

            if (StageLink::deserializeOutputList(
                    incomingPacket.payload,
                    incomingPacket.payloadLength,
                    list
                ))
            {
                remoteDevice.setOutputList(list);
                remoteDevice.logSummary();
            }
            else
            {
                Serial.println("Malformed output list response received");
            }
        }
    }

    int encoderTurn = Encoder::consumeTurn();
    if (encoderTurn != 0)
    {
        // Apply as a relative step, not a recomputation from absolute
        // position - this is what makes each channel's limit clamp
        // without windup (see controlOutputs / OutputLimiter.h).
        const ControlChannelConfig &activeConfig = CONTROL_CHANNELS[static_cast<uint8_t>(currentMode)];
        StageLink::applyLimitedOutputDelta(
            controlOutputs[static_cast<uint8_t>(currentMode)],
            encoderTurn * activeConfig.stepPerClick
        );

        Serial.print("Encoder: ");
        Serial.println(Encoder::position());

        sendEncoderValue();
        if (LIVE_OUTPUT_CONTROL_ENABLED)
        {
            sendControlValue(currentMode);
        }
        showCurrentPage();
    }

    // The encoder button has two jobs: a short press cycles which channel
    // the encoder drives (Servo -> Red -> Green -> Blue -> Brightness ->
    // Servo...), a hold past PAGE_CYCLE_HOLD_MS instead cycles the OLED
    // page (Status -> Diagnostics -> Device Info -> Status...). The page
    // cycle fires the moment the hold threshold elapses, while the
    // button is still down, rather than waiting for release, so the
    // display switches pages as immediate feedback that the hold
    // registered; a short press can only be resolved at release, since
    // there's no way to know it won't turn into a hold before then.
    // BUTTON_EVENT still goes to RX on every press/release edge exactly
    // as before, unaffected by any of this - only the local page/mode
    // dispatch changed.
    //
    // The state-change edge is consumed first, so a fresh press always
    // resets encoderButtonPressStartTime before the hold check below
    // ever runs - checking hold duration first would (and did on RX,
    // same bug) read the previous press's stale start time on the very
    // iteration a new press begins, firing the hold action instantly on
    // a short click.
    bool encoderButtonPressed;
    if (Encoder::consumeButtonStateChange(encoderButtonPressed))
    {
        sendEncoderButtonState(encoderButtonPressed);

        if (encoderButtonPressed)
        {
            encoderButtonPressStartTime = millis();
            encoderHoldTriggered = false;
        }
        else if (encoderHoldTriggered)
        {
            // This release just ended the hold that already cycled the
            // page below - nothing more to do for it.
            encoderHoldTriggered = false;
        }
        else
        {
            currentMode = static_cast<ControlMode>(
                (static_cast<uint8_t>(currentMode) + 1) % CONTROL_MODE_COUNT
            );

            Serial.print("Control mode: ");
            Serial.println(CONTROL_CHANNELS[static_cast<uint8_t>(currentMode)].label);
        }

        // Refreshes on every press/release edge (not just mode changes)
        // so the Status page's live "Button: PRESSED/RELEASED" line
        // updates immediately rather than waiting for the next periodic
        // refresh - unchanged from before this hold-timing change.
        showCurrentPage();
    }

    if (!encoderHoldTriggered &&
        Encoder::isButtonPressed() &&
        millis() - encoderButtonPressStartTime >= PAGE_CYCLE_HOLD_MS)
    {
        pageCycler.next();
        showCurrentPage();
        encoderHoldTriggered = true;
    }

    // Track delivery outcomes for our own sends. The only one we act on
    // here is Heartbeat: its ack is what proves the link just came back
    // up, which is when a fresh STATE_SNAPSHOT needs to go out (a failed
    // heartbeat sets the same flag so the snapshot fires on the next
    // successful one instead of being lost).
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

            if (result.type == StageLink::PacketType::Heartbeat &&
                capabilityRequestNeeded)
            {
                sendCapabilityRequest();
                capabilityRequestNeeded = false;
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
                capabilityRequestNeeded = true;
            }
        }
    }

    // Heartbeat is what ReliableRadio/isPeerOnline uses to detect the link
    // is alive - see ONLINE_TIMEOUT_MS in ReliableRadio.cpp.
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

    bool peerOnline = radio.isPeerOnline();
    remoteDevice.setConnected(peerOnline);

    if (peerOnline)
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
