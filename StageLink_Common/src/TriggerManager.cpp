#include "TriggerManager.h"

void StageLink::TriggerManager::begin(EffectEngine &effectEngine)
{
    engine = &effectEngine;
}

void StageLink::TriggerManager::assignTrigger(uint8_t triggerID, uint8_t effectID)
{
    for (uint8_t i = 0; i < count; ++i)
    {
        if (bindings[i].triggerID == triggerID)
        {
            bindings[i].effectID = effectID;
            return;
        }
    }

    if (count >= MAX_TRIGGERS)
    {
        return;
    }

    bindings[count].triggerID = triggerID;
    bindings[count].effectID = effectID;
    count++;
}

void StageLink::TriggerManager::clearTrigger(uint8_t triggerID)
{
    assignTrigger(triggerID, NO_EFFECT_ASSIGNED);
}

uint8_t StageLink::TriggerManager::getAssignedEffect(uint8_t triggerID) const
{
    for (uint8_t i = 0; i < count; ++i)
    {
        if (bindings[i].triggerID == triggerID)
        {
            return bindings[i].effectID;
        }
    }

    return NO_EFFECT_ASSIGNED;
}

void StageLink::TriggerManager::trigger(uint8_t triggerID)
{
    if (engine == nullptr)
    {
        return;
    }

    if (getAssignedEffect(triggerID) == NO_EFFECT_ASSIGNED)
    {
        return;
    }

    engine->startEffect();
}
