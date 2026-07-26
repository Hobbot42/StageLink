// StageLink RxQ Action
// The unit of "what to do" inside a cue - see ShowEngine.h. First
// version: point an OutputManager channel (outputId) at a value via one
// command type, LEVEL (directly set the value - no ramping, fading,
// timing, randomness, or any other command yet; see
// actionCommandName()). A plain data holder - the only thing that ever
// turns an Action into an OutputManager call is
// ShowEngine::executeCurrentCue().
// Belongs to: StageLink_Rx.

#pragma once

#include <cstdint>

enum class ActionCommand : uint8_t
{
    Level = 0
    // FADE/WAIT/RANDOM/EFFECT/PULSE intentionally not added yet.
};

// Debug/display name - single source of truth so future callers (a
// GUI, Serial logging) don't hardcode this string themselves. Same
// convention as StageLink_Common/src/StageLinkProtocol.h's
// packetTypeName() / DeviceInfo.h's capabilityName().
const char *actionCommandName(ActionCommand command);

struct Action
{
    // OutputManager channel number - same numbering RX main.cpp's
    // OUTPUT_CHANNEL_* constants and VALUE_UPDATE/STATE_SNAPSHOT
    // routing already use (see OutputManager.h). Not validated here -
    // OutputManager::update() already handles an unregistered channel
    // safely (returns false, no-op).
    uint8_t outputId;
    ActionCommand command;
    int32_t value;
};
