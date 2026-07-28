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
    uint32_t cueFadeMs
)
{
    const uint32_t now = millis();

    for (uint8_t i = 0; i < actionCount; ++i)
    {
        const Action &action = actions[i];

        switch (action.command)
        {
            case ActionCommand::Level:
            {
                // An action's own fade wins; ACTION_FADE_FROM_CUE means it
                // never got one, so the cue's time applies.
                const uint32_t fadeMs = action.fadeTenths == ACTION_FADE_FROM_CUE
                                            ? cueFadeMs
                                            : static_cast<uint32_t>(action.fadeTenths) * 100;
                const uint32_t delayMs = static_cast<uint32_t>(action.delayTenths) * 100;

                cancelChannel(action.outputId);

                if (delayMs == 0 && fadeMs == 0)
                {
                    // Nothing to schedule - same immediate behaviour this
                    // had before any timing existed.
                    outputManager.update(action.outputId, action.value);
                    break;
                }

                for (uint8_t slot = 0; slot < MAX_RAMPS; ++slot)
                {
                    if (ramps_[slot].active)
                    {
                        continue;
                    }

                    ramps_[slot].channel = action.outputId;
                    ramps_[slot].to = action.value;
                    ramps_[slot].startMs = now + delayMs;
                    ramps_[slot].durationMs = fadeMs;
                    ramps_[slot].active = true;

                    // "from" is deliberately not captured here. A delayed
                    // action starts from wherever the output is when its
                    // delay expires, which may be mid-fade from something
                    // else - capturing now would ramp from a value that is
                    // already stale by the time it matters.
                    ramps_[slot].started = false;
                    ramps_[slot].from = 0;
                    break;
                }
                break;
            }

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

        // Signed difference so a start time still in the future reads as
        // negative rather than wrapping to a huge unsigned value.
        const int32_t sinceStart = static_cast<int32_t>(now - ramp.startMs);
        if (sinceStart < 0)
        {
            continue; // still waiting out its delay
        }

        if (!ramp.started)
        {
            ramp.from = outputManager.lastValue(ramp.channel);
            ramp.started = true;
        }

        const uint32_t elapsed = static_cast<uint32_t>(sinceStart);

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
