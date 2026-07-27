#include "OutputManager.h"

bool StageLink::OutputManager::registerDevice(uint8_t channel, OutputDevice *device)
{
    if (count >= MAX_DEVICES || device == nullptr)
    {
        return false;
    }

    bindings[count].channel = channel;
    bindings[count].device = device;
    bindings[count].lastValue = 0;
    count++;

    return device->begin();
}

bool StageLink::OutputManager::update(uint8_t channel, int32_t value)
{
    for (uint8_t i = 0; i < count; ++i)
    {
        if (bindings[i].channel == channel)
        {
            bindings[i].device->update(value);
            bindings[i].lastValue = value;
            return true;
        }
    }

    return false;
}

void StageLink::OutputManager::refreshAll()
{
    for (uint8_t i = 0; i < count; ++i)
    {
        bindings[i].device->refreshState();
    }
}

int32_t StageLink::OutputManager::lastValue(uint8_t channel) const
{
    for (uint8_t i = 0; i < count; ++i)
    {
        if (bindings[i].channel == channel)
        {
            return bindings[i].lastValue;
        }
    }

    return 0;
}
