// StageLink RxQ Action
// The unit of "what to do" inside a cue - see ShowEngine.h. Points an
// OutputManager channel (outputId) at a value via a command type. Only
// LEVEL is implemented (directly set the value - no ramping, fading,
// timing, or randomness); COLOR and STATE exist as reserved command IDs
// only - see ActionEngine.h for what happens (nothing) if one is used.
// A plain data holder - the only thing that ever turns an Action into
// an OutputManager call is ActionEngine::executeActions().
// Belongs to: StageLink_Rx.

#pragma once

#include <cstdint>

enum class ActionCommand : uint8_t
{
    Level = 0,
    Color = 1, // placeholder - not implemented yet, see ActionEngine.h
    State = 2  // placeholder - not implemented yet, see ActionEngine.h
    // FADE/WAIT/RANDOM/EFFECT/PULSE intentionally not added yet either.
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
