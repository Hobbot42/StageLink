# StageLink Project Documentation

## Project Overview

StageLink is a modular wireless show-control system for theater, themed entertainment, and animatronics. The long-term goal is to create a reliable, easy-to-use controller that allows complex shows to be programmed and executed without requiring a computer during normal operation.

The system is designed around two primary devices:

* **RxQ** – The show controller. Stores shows, executes cues, controls outputs, and becomes the permanent installation.
* **TxQ** – A handheld wireless controller used for remote GO, HOME, monitoring, diagnostics, and eventually remote programming.

A major architectural decision was made during development:

**Shows live on the RxQ, not the TxQ.**

The TxQ should become a remote interface rather than a second show controller. This simplifies synchronization, allows multiple transmitters in the future, and makes every installation self-contained.

Primary design goals:

* Extremely reliable operation.
* Easy to understand.
* Minimal setup.
* Modular outputs.
* Generic architecture allowing future expansion.
* No unnecessary complexity.
* Designed specifically for theater and themed entertainment workflows.

---

# Hardware

## RxQ Controller

Current hardware:

* ESP32-WROOM module (AITRIP board)
* SH1106 OLED display
* Rotary encoder
* Encoder push button
* Dedicated Back button
* Dedicated Action button
* ESP-NOW wireless radio

Earlier prototypes used SSD1306 displays before switching to SH1106.

---

## Output Architecture

One of the largest design changes was replacing dedicated hardware functions with generic output ports.

Outputs are now:

* OUT-01
* OUT-02
* OUT-03
* OUT-04

Each output is configured during setup.

Possible output types:

* Servo
* Stepper
* WS2812 LED
* Digital Output (MOSFET driven)
* H-Bridge Output (future)

Originally the project considered generic "GPIO" ports.

That evolved into:

PORT

Then finally became:

OUT-01

which was considered much clearer for operators.

---

## Inputs

Inputs are separate from outputs.

Current design:

* IN-01
* IN-02

Each input uses:

* Signal
* Ground

No second GPIO is required.

Future options:

* Trigger to ground
* Trigger to +3.3V
* Pull-up
* Pull-down

Protection circuitry is planned before exposing them.

---

## Output Driver Philosophy

Outputs are intended to drive logic-level devices only.

StageLink does **not** directly drive:

* motors
* relays
* solenoids

Instead:

Outputs drive:

* MOSFET gate
* Servo signal
* Step/Dir inputs
* WS2812 data

This keeps electrical specifications simple.

---

## OLED

Current display:

* SH1106
* I2C

Working correctly.

---

## Encoder

Uses quadrature encoder.

A transitions-per-step issue was corrected during development.

---

## Buttons

Current buttons:

* Encoder button
* Back
* Action

Originally encoder press/hold performed many functions.

Later replaced with dedicated buttons.

---

## Radio

ESP-NOW

Reliable.

RSSI monitored.

ACK system implemented.

---

# Architecture & Protocol

## High-Level Architecture

Current architecture:

ReliableRadio

↓

ShowEngine

↓

ActionEngine

↓

OutputManager

↓

OutputDevice

↓

Hardware

This separation is intentional.

---

## ReliableRadio

Responsibilities:

* ESP-NOW communication
* ACK
* Retries
* Connection monitoring
* RSSI
* Diagnostics

Does NOT know anything about shows.

---

## ShowEngine

Responsibilities:

* Current show
* Current cue
* Selected cue
* Navigation
* GO
* HOME

Does NOT know:

* Servo
* LED
* Relay

ShowEngine simply requests:

Execute Cue

---

## ActionEngine

Added after realizing ShowEngine should not directly control outputs.

Responsibilities:

Receive Actions.

Loop through Actions.

Forward each Action to OutputManager.

Current implementation:

Supports:

Level

Placeholder:

Color
