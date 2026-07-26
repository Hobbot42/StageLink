#include "Encoder.h"

// StageLink Encoder

namespace
{
    constexpr unsigned long BUTTON_DEBOUNCE_MS = 30;
    // Set by begin() - how many valid quadrature transitions this board's
    // encoder produces per physical click, so movement only commits once
    // that many transitions have accumulated in one direction. TX and RX
    // have confirmed different values here (different physical encoders),
    // so this is per-instance via begin(), not a shared constant.
    int quadratureTransitionsPerStep = 4;

    // Lookup table of movement (-1/0/+1) indexed by
    // (previousState << 2 | currentState), where state is the 2-bit
    // (CLK<<1 | DT) reading. 0 entries are either "no change" or an
    // impossible/bounced transition, both treated as no movement.
    constexpr int8_t quadratureTransitions[] =
    {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };

    uint8_t clockPin = 0;
    uint8_t dataPin = 0;
    uint8_t buttonPin = 0;

    portMUX_TYPE encoderLock = portMUX_INITIALIZER_UNLOCKED;

    volatile int encoderPosition = 0;
    volatile int pendingTurn = 0;
    volatile int quadratureTransitionsSinceStep = 0;

    volatile uint8_t lastQuadratureState = 0;
    int lastButtonReading = HIGH;
    int stableButtonState = HIGH;
    unsigned long buttonChangedTime = 0;
    bool buttonPressEvent = false;
    bool buttonStateChangeEvent = false;
    bool encoderButtonIsPressed = false;
}

void Encoder::begin(
    uint8_t newClockPin,
    uint8_t newDataPin,
    uint8_t newButtonPin,
    int transitionsPerStep
)
{
    clockPin = newClockPin;
    dataPin = newDataPin;
    buttonPin = newButtonPin;
    quadratureTransitionsPerStep = transitionsPerStep;

    pinMode(clockPin, INPUT_PULLUP);
    pinMode(dataPin, INPUT_PULLUP);
    pinMode(buttonPin, INPUT_PULLUP);

    lastQuadratureState =
        (digitalRead(clockPin) << 1) |
        digitalRead(dataPin);
    lastButtonReading = digitalRead(buttonPin);
    stableButtonState = lastButtonReading;
    encoderButtonIsPressed = stableButtonState == LOW;

    // Both CLK and DT get interrupts (not just CLK) so no quadrature edge
    // is ever missed between polls - rotation direction depends on seeing
    // every transition in order.
    attachInterrupt(
        digitalPinToInterrupt(clockPin),
        Encoder::handleRotationInterrupt,
        CHANGE
    );

    attachInterrupt(
        digitalPinToInterrupt(dataPin),
        Encoder::handleRotationInterrupt,
        CHANGE
    );
}

void Encoder::update()
{
    // Standard debounce: only commit a reading once it has held steady for
    // BUTTON_DEBOUNCE_MS, so mechanical contact bounce doesn't register as
    // multiple presses.
    int buttonReading = digitalRead(buttonPin);
    if (buttonReading != lastButtonReading)
    {
        buttonChangedTime = millis();
    }

    if (millis() - buttonChangedTime >= BUTTON_DEBOUNCE_MS &&
        buttonReading != stableButtonState)
    {
        stableButtonState = buttonReading;
        encoderButtonIsPressed = stableButtonState == LOW;
        buttonStateChangeEvent = true;

        if (encoderButtonIsPressed)
        {
            buttonPressEvent = true;
        }
    }

    lastButtonReading = buttonReading;
}

int Encoder::position()
{
    portENTER_CRITICAL(&encoderLock);
    int currentPosition = encoderPosition;
    portEXIT_CRITICAL(&encoderLock);

    return currentPosition;
}

int Encoder::consumeTurn()
{
    portENTER_CRITICAL(&encoderLock);
    int turn = pendingTurn;
    pendingTurn = 0;
    portEXIT_CRITICAL(&encoderLock);

    return turn;
}

bool Encoder::buttonPressed()
{
    bool pressed = buttonPressEvent;
    buttonPressEvent = false;
    return pressed;
}

bool Encoder::consumeButtonStateChange(bool &pressed)
{
    if (!buttonStateChangeEvent)
    {
        return false;
    }

    pressed = encoderButtonIsPressed;
    buttonStateChangeEvent = false;
    return true;
}

bool Encoder::isButtonPressed()
{
    return encoderButtonIsPressed;
}

void IRAM_ATTR Encoder::handleRotationInterrupt()
{
    uint8_t currentState =
        (digitalRead(clockPin) << 1) |
        digitalRead(dataPin);

    // Look up how this state pair moves the encoder (see the transition
    // table above), then accumulate until a full detent's worth of
    // transitions has been seen before committing a step.
    uint8_t transition = (lastQuadratureState << 2) | currentState;
    int8_t movement = quadratureTransitions[transition];

    if (movement != 0)
    {
        portENTER_CRITICAL_ISR(&encoderLock);

        quadratureTransitionsSinceStep += movement;

        if (quadratureTransitionsSinceStep >= quadratureTransitionsPerStep)
        {
            encoderPosition++;
            pendingTurn++;
            quadratureTransitionsSinceStep = 0;
        }
        else if (quadratureTransitionsSinceStep <= -quadratureTransitionsPerStep)
        {
            encoderPosition--;
            pendingTurn--;
            quadratureTransitionsSinceStep = 0;
        }

        portEXIT_CRITICAL_ISR(&encoderLock);
    }

    lastQuadratureState = currentState;
}
