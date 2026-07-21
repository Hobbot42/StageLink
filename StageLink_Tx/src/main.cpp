// StageLink TX (handheld control unit)
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

namespace
{
uint8_t receiverAddress[] = {
    0x68, 0x09, 0x47, 0x3C, 0x49, 0xB4
};

// Rotary encoder + integrated button pins.
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

// TX has no physical servo - these describe how RX's servo angle is
// derived from the encoder, since TX is the source of truth for it.
// The angle is accumulated from relative encoder steps (via
// OutputLimiter, see servoOutput below) rather than recomputed from
// Encoder::position() directly, so it clamps without windup: once a
// limit is hit, further movement in that direction is dropped instead of
// silently building up, and reversing direction responds immediately
// instead of first having to travel back through the clamped range.
constexpr int SERVO_MIN_ANGLE = 0;
constexpr int SERVO_MAX_ANGLE = 180;
constexpr int SERVO_CENTER_ANGLE = 90;
constexpr int SERVO_DEGREES_PER_STEP = 2;

// STATE_SNAPSHOT channel IDs (application-level, not part of the wire
// protocol itself - see StateSnapshot.h). Gaps between numbers are
// intentional, leaving room for future channels within each group.
constexpr uint8_t CHANNEL_ENCODER = 1;
constexpr uint8_t CHANNEL_BUTTON = 2;
constexpr uint8_t CHANNEL_SERVO = 10;

unsigned long lastHeartbeatTime = 0;
unsigned long lastDisplayRefresh = 0;
bool buttonWasPressed = false;
bool inputStateSnapshotNeeded = true;
bool displayReady = false;
StageLink::StatusPageCycler pageCycler;
StageLink::ReliableRadio radio;

// Servo angle, clamped at every update (see loop()) rather than derived
// fresh from Encoder::position() each time - this is what gives the
// servo limit its "no windup" behavior.
StageLink::LimitedOutput servoOutput;

int currentServoAngle()
{
    return servoOutput.value;
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
            currentServoAngle()
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

void sendServoAngle()
{
    int angle = currentServoAngle();

    uint8_t valuePayload[StageLink::VALUE_UPDATE_PAYLOAD_SIZE];
    StageLink::encodeValueUpdate(
        StageLink::ValueChannel::Servo,
        angle,
        valuePayload
    );

    if (radio.send(
            StageLink::PacketType::VALUE_UPDATE,
            valuePayload,
            sizeof(valuePayload)
        ))
    {
        Serial.print("Queued servo angle: ");
        Serial.println(angle);
    }
    else
    {
        Serial.println("Servo queue full");
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
    StageLink::addOrUpdateStateItem(
        snapshot,
        CHANNEL_SERVO,
        StageLink::StateItemType::Servo,
        currentServoAngle()
    );

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

    Encoder::begin(
        ENCODER_CLK_PIN,
        ENCODER_DT_PIN,
        ENCODER_BUTTON_PIN
    );

    StageLink::initLimitedOutput(
        servoOutput,
        SERVO_CENTER_ANGLE,
        SERVO_MIN_ANGLE,
        SERVO_MAX_ANGLE
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

    // RX asks for this after it (re)connects and doesn't yet have TX's state.
    StageLink::Packet incomingPacket;
    while (radio.receive(incomingPacket))
    {
        if (incomingPacket.type == StageLink::PacketType::STATE_REQUEST)
        {
            Serial.println("State request received");
            sendCurrentInputState();
        }
    }

    int encoderTurn = Encoder::consumeTurn();
    if (encoderTurn != 0)
    {
        // Apply as a relative step, not a recomputation from absolute
        // position - this is what makes the servo limit clamp without
        // windup (see servoOutput / OutputLimiter.h).
        StageLink::applyLimitedOutputDelta(servoOutput, encoderTurn * SERVO_DEGREES_PER_STEP);

        Serial.print("Encoder: ");
        Serial.println(Encoder::position());

        sendEncoderValue();
        sendServoAngle();
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
