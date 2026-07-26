# StageLink — Claude Code Instructions

## What this project is
StageLink is a modular wireless show-control system for theater, themed 
entertainment, and animatronics, built on ESP32 hardware. Two devices:
- **RxQ** — the show controller. Stores shows/cues, executes them, drives 
  outputs. Stays installed on-site.
- **TxQ** — a handheld wireless remote (GO/HOME, monitoring, diagnostics).

Shows live entirely on the RxQ, not the TxQ — the TxQ is a thin remote, 
not a second controller. This is a deliberate architectural decision, 
don't reintroduce show storage on the TxQ side without asking first.

Full design history, hardware details, and roadmap are in PROJECT_NOTES.md 
at the repo root — read it for context on anything not covered here.

## Repo structure
- StageLink_Common/ — shared source (src + scripts), used by both projects
- StageLink_Rx/ — RxQ firmware, PlatformIO project
- StageLink_Tx/ — TxQ firmware, PlatformIO project

Both StageLink_Rx/platformio.ini and StageLink_Tx/platformio.ini depend 
on StageLink_Common via lib_extra_dirs and a pre-build script 
(generate_build_info.py). Never suggest deleting or moving 
StageLink_Common — both builds fail without it.

## Architecture
Data flows in one direction through these layers:
ReliableRadio, then ShowEngine, then ActionEngine, then OutputManager, 
then OutputDevice, then Hardware.

Each layer only knows about the layer directly below it:
- ReliableRadio — ESP-NOW comms, ACK, retries, RSSI. Knows nothing about shows.
- ShowEngine — cue navigation, GO/HOME. Knows nothing about servos/LEDs/relays.
- ActionEngine — loops over a flat action list, forwards each to OutputManager.
- OutputManager — routes generic actions to the correct OutputDevice.
- OutputDevice — hardware-specific behavior (ServoOutput, LEDOutput, etc.)

Keep this separation when adding features — don't let ShowEngine reach 
past ActionEngine, don't let ActionEngine know about cues/shows.

## Naming conventions
- System-level names are uppercase: OUT-01, IN-01
- User-created names use normal capitalization: Dragon Head, Fire Burst

## Git workflow rules
- Never stage or commit machine-specific values in platformio.ini 
  (upload_port, monitor_port) — these differ per computer and should 
  stay as local uncommitted changes.
- Keep commits scoped — don't bundle unrelated changes (e.g. a 
  gitignore cleanup and a feature change) into one commit.
- Write accurate commit messages — describe what actually changed, 
  don't guess or use generic messages like "update."
- This repo lives on a Mac, opened via VS Code. /dev/cu.wchusbserial... 
  style port names are normal and expected.

## Known open issues (see PROJECT_NOTES.md for full list)
- ActionEngine only implements the Level command; Color and State 
  are placeholders, not yet functional.
- No cue/action editing UI yet.
- No flash persistence yet — shows aren't saved across reboots.
- ShowEngine still uses a hard-coded test show, not real show storage.
