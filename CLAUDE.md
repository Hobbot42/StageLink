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

Show-related code lives in StageLink_Rx/src: ShowEngine (the show data and
all editing), ActionEngine, GuiController (the operator UI), ShowStorage
(flash persistence). Shows are RxQ-only, so none of it belongs in
StageLink_Common.

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
- ShowEngine — owns every show on the device: cue navigation, GO/HOME, and
  all show/cue/action editing. Knows nothing about servos/LEDs/relays.
- ActionEngine — loops over a flat action list, forwards each to OutputManager.
- OutputManager — routes generic actions to the correct OutputDevice.
- OutputDevice — hardware-specific behavior (ServoOutput, LEDOutput, etc.)

Keep this separation when adding features — don't let ShowEngine reach 
past ActionEngine, don't let ActionEngine know about cues/shows.

GuiController is a view on top of this, not a layer in it. Both Show Mode 
and Program Mode read and write the real ShowEngine — there is no second 
copy of show data anywhere. It never calls OutputManager directly either; 
even Value Entry's live preview goes through ActionEngine, so editing 
drives hardware over the same path GO does.

## Show data model
Shows and cues carry an internal ID allocated once and never reused. The 
execution number (Q01, Q02...) is NOT stored — it is the object's position 
in the list, so moving and deleting can't leave numbering out of step with 
what actually runs.

Names default to "Show 01"/"Cue 01" and follow position while `autoName` 
is true; renaming clears the flag and the custom name is then preserved 
through every move.

One edited action in the GUI can be several stored Actions: the engine 
stores one Action per output *channel*, so an LED color is four 
(R/G/B/Brightness) while a servo position is one. The Action List groups 
them back into one row per output. ActionEngine stays unaware of this and 
keeps consuming a flat array.

Programming rule: one action per output per cue. Add Action enforces this 
by hiding outputs the cue already drives.

## Persistence
ShowEngine saves to flash (via ShowStorage, NVS) about 1.5s after editing 
stops — a burst of encoder clicks costs one write, not one per click.

**If you change the Show/Cue/Action struct layout, bump 
`ShowStorage::LAYOUT_VERSION`.** Stored data is validated by both that 
version and the record size; a mismatch discards saved shows rather than 
reinterpreting old bytes as cues. Discarding is deliberate — a show built 
from garbage would drive real hardware.

The demo show (`ShowEngine::loadTestShow()`, behind main.cpp's 
`LOAD_TEST_SHOW_ON_BOOT`) is test data only, and only seeds a unit with 
nothing stored. A shipped unit boots with no shows in firmware. Never make 
it run unconditionally — it would wipe saved shows on every boot.

## Uploading
VS Code's Upload button (the arrow) picks the transport automatically via 
StageLink_Rx/scripts/auto_upload.py: USB when a board is plugged in, OTA 
when none is. OTA needs no configuration — it addresses the board by the 
mDNS hostname ArduinoOTA advertises (stagelink-rx.local) and reads the OTA 
password from the gitignored src/secrets.h the firmware compiles it from. 
`RXQ_OTA_IP`/`RXQ_OTA_PASSWORD` override both; `pio run -e rxq_ota -t 
upload` forces OTA.

OTA requires the RxQ to be in Update Mode (Setup > Update Mode). Never put 
real credentials in secrets.example.h — it is tracked; secrets.h is not.

## Naming conventions
- System-level names are uppercase: OUT-01, IN-01
- User-created names use normal capitalization: Dragon Head, Fire Burst
- The RxQ calls itself a "controller" in the UI, not a "unit" 
  (Setup > Controller Label).

## UI rules
- Rotate navigates or edits a value, a short press selects/confirms/GOes, 
  and the dedicated Back button goes back or cancels one step.
- Don't add per-screen gesture hints for the standard controls — rotate,
  press and Back behave the same everywhere, so a reminder just costs a
  row. A hint is warranted only for a gesture that exists on one screen
  alone and can't be discovered: the name-entry screen's "Hold = Delete
  all" is the one case.
- Holding confirm (encoder or Action button) is a real gesture in the
  GUI, handled by GuiController::handleHoldConfirm(). It is a no-op
  except on the name-entry screens. This is not the old "hold = back"
  convention, which stays removed.
- Don't ship a control that does nothing. A field or command with no 
  implementation behind it gets removed, not left as a placeholder.
- ">" marks the cursor. "-" marks the running cue on the Show Run screen, 
  and is used nowhere else.

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
- Only the Level action command is implemented. Color and State are
  reserved ActionCommand values that ActionEngine ignores. An LED colour
  works today because it is stored as Level on four channels, not because
  Color is implemented.
- Actions have no delay or fade of their own. A *cue* has a fade time and
  ActionEngine can ramp, so the machinery exists - what is missing is
  per-action timing, which is what staggers a sequence rather than moving
  everything together.
- No auto-follow, hold or loop. A cue is a set of actions with a fade
  time; GO can interrupt at any time and nothing advances on its own.
- Capacity is low and arbitrary: 4 shows, 8 cues per show, 8 actions per
  cue. RAM is only 18% used, so these can be raised - but the stored
  layout changes, so bump ShowStorage::LAYOUT_VERSION with it, and keep
  one show under the ~4KB NVS entry limit.
- No inputs. TriggerManager exists and drives the legacy effect test, but
  nothing routes a physical trigger to a cue.
- No status/output monitor page showing live output values. OutputManager
  ::lastValue() already tracks them, so this is mostly a display screen.
- No show backup/restore.
- TxQ hasn't been updated for any of the show editing work.
- The legacy diagnostic pages (Status/Diagnostics/Effect Test/Trigger
  Status, reached via Setup > Diagnostics) still use the local button's
  hold gesture. The GUI does not - it has a dedicated Back button. Don't
  reintroduce "hold = back" language in GUI screens.
