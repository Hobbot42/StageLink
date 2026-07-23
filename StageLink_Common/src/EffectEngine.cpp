#include "EffectEngine.h"

#include <Arduino.h>

void StageLink::EffectEngine::begin(OutputManager &manager)
{
    outputManager = &manager;
}

void StageLink::EffectEngine::loadEffect(const Effect &effect)
{
    loadedEffect = effect;
    hasLoadedEffect = true;
    running = false;
    currentStepIndex = 0;
}

void StageLink::EffectEngine::startEffect()
{
    if (outputManager == nullptr || !hasLoadedEffect || loadedEffect.stepCount == 0)
    {
        return;
    }

    currentStepIndex = 0;
    running = true;
    executeCurrentStep();
}

void StageLink::EffectEngine::stopEffect()
{
    running = false;
}

bool StageLink::EffectEngine::isRunning() const
{
    return running;
}

void StageLink::EffectEngine::update()
{
    if (!running)
    {
        return;
    }

    const EffectStep &step = loadedEffect.steps[currentStepIndex];

    if (millis() - stepStartTime < step.delayAfterMs)
    {
        return;
    }

    uint8_t nextIndex = currentStepIndex + 1;

    if (nextIndex >= loadedEffect.stepCount)
    {
        running = false;
        return;
    }

    currentStepIndex = nextIndex;
    executeCurrentStep();
}

void StageLink::EffectEngine::executeCurrentStep()
{
    const EffectStep &step = loadedEffect.steps[currentStepIndex];
    outputManager->update(step.channel, step.value);
    stepStartTime = millis();
}
