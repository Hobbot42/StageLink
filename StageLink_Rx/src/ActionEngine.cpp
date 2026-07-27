#include "ActionEngine.h"

#include <Arduino.h>

void ActionEngine::cancelChannel(uint8_t channel)
{
    for (uint8_t i = 0; i < MAX_RAMPS; ++i)
    {
        if (ramps_[i].active && ramps_[i].channel == channel)
        {
            ramps_[i].active = false;
        }
    }
}

void ActionEngine::cancelAll()
{
    for (uint8_t i = 0; i < MAX_RAMPS; ++i)
    {
        ramps_[i].active = false;
    }
}

bool ActionEngine::isFading() const
{
    for (uint8_t i = 0; i < MAX_RAMPS; ++i)
    {
        if (ramps_[i].active)
        {
            return true;
        }
    }

    return false;
}

void ActionEngine::executeActions(
    const Action *actions,
    uint8_t actionCount,
    StageLink::OutputManager &outputManager,
    uint32_t fadeMs
)
{
    const uint32_t now = millis();

    for (uint8_t i = 0; i < actionCount; ++i)
    {
        const Action &action = actions[i];

        switch (action.command)
        {
            case ActionCommand::Level:
                if (fadeMs == 0)
                {
                    // No fade - same immediate behaviour this had before
                    // fading existed. Any ramp on the channel is dropped
                    // first, or it would carry on fighting this value.
                    cancelChannel(action.outputId);
                    outputManager.update(action.outputId, action.value);
                    break;
                }

                cancelChannel(action.outputId);

                for (uint8_t slot = 0; slot < MAX_RAMPS; ++slot)
                {
                    if (ramps_[slot].active)
                    {
                        continue;
                    }

                    // Starts from where the output actually is, so a cue
                    // fired mid-fade continues from the value reached
                    // rather than snapping back to a stale start point.
                    ramps_[slot].channel = action.outputId;
                    ramps_[slot].from = outputManager.lastValue(action.outputId);
                    ramps_[slot].to = action.value;
                    ramps_[slot].startMs = now;
                    ramps_[slot].durationMs = fadeMs;
                    ramps_[slot].active = true;
                    break;
                }
                break;

            case ActionCommand::Color:
            case ActionCommand::State:
                break; // recognized, not implemented yet - see Action.h
        }
    }
}

void ActionEngine::tick(StageLink::OutputManager &outputManager)
{
    const uint32_t now = millis();

    for (uint8_t i = 0; i < MAX_RAMPS; ++i)
    {
        Ramp &ramp = ramps_[i];
        if (!ramp.active)
        {
            continue;
        }

        const uint32_t elapsed = now - ramp.startMs;

        if (ramp.durationMs == 0 || elapsed >= ramp.durationMs)
        {
            // Lands exactly on the target rather than wherever the last
            // interpolation step happened to fall.
            outputManager.update(ramp.channel, ramp.to);
            ramp.active = false;
            continue;
        }

        // 64-bit intermediate: a full 0-255 swing over 99.9s is well
        // within 32 bits, but the multiply is what overflows first if
        // either the range or the duration grows later.
        const int64_t span = static_cast<int64_t>(ramp.to) - static_cast<int64_t>(ramp.from);
        const int64_t moved = span * static_cast<int64_t>(elapsed) / static_cast<int64_t>(ramp.durationMs);

        outputManager.update(ramp.channel, ramp.from + static_cast<int32_t>(moved));
    }
}
