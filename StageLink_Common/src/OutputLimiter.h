// StageLink OutputLimiter
// Generic clamped-accumulator building block for any relative
// (delta-driven) output - today's servo angle, and future outputs like
// stepper position, DMX level, or a digital output's on/off state.
//
// Problem this solves: if a relative input (e.g. encoder steps) is
// summed unclamped and only clamped at the point of use, an operator who
// keeps turning past a limit builds up "hidden" movement that isn't
// reflected in the output. Reversing direction then has to first undo
// all of that hidden movement before the output visibly responds again.
// Clamping the accumulator itself instead means the stored value never
// exceeds its limit, so a reversed delta moves the output immediately.
//
// Belongs to: StageLink_Common. Not tied to any specific hardware -
// callers own their own LimitedOutput instance and decide what unit
// (degrees, steps, DMX levels, ...) the value represents.

#pragma once

#include <Arduino.h>

namespace StageLink
{
    struct LimitedOutput
    {
        int32_t value;
        int32_t minValue;
        int32_t maxValue;
    };

    // Sets the allowed range and starting value. Call once, e.g. in setup().
    inline void initLimitedOutput(
        LimitedOutput &output,
        int32_t initialValue,
        int32_t minValue,
        int32_t maxValue
    )
    {
        output.minValue = minValue;
        output.maxValue = maxValue;
        output.value = constrain(initialValue, minValue, maxValue);
    }

    // Changes the allowed range without discarding the current value
    // (re-clamping it if it now falls outside the new range). For future
    // use if limits ever become remotely configurable.
    inline void setLimitedOutputRange(
        LimitedOutput &output,
        int32_t minValue,
        int32_t maxValue
    )
    {
        output.minValue = minValue;
        output.maxValue = maxValue;
        output.value = constrain(output.value, minValue, maxValue);
    }

    // Applies one relative step. The clamp is on the accumulator itself,
    // not just the value returned to callers - so movement beyond a
    // limit is dropped rather than stored, and the next delta in the
    // opposite direction moves the output immediately, with no "unwind"
    // required first.
    inline int32_t applyLimitedOutputDelta(LimitedOutput &output, int32_t delta)
    {
        output.value = constrain(output.value + delta, output.minValue, output.maxValue);
        return output.value;
    }
}
