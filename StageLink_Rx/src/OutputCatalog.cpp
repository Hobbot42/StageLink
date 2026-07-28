#include "OutputCatalog.h"

#include <cstdio>
#include <cstring>
#include "ConfigManager.h"

void StageLink::OutputCatalog::buildNameKey(uint8_t index, char *buffer, uint8_t bufferSize)
{
    snprintf(buffer, bufferSize, "out%uname", static_cast<unsigned>(index));
}

void StageLink::OutputCatalog::begin()
{
    // Nothing to load up front - each output picks up its stored name as
    // it is declared, in addOutput().
    count_ = 0;
}

const char *StageLink::OutputCatalog::typeName(Type type)
{
    switch (type)
    {
        case Type::Servo:
            return "SERVO";
        case Type::Led:
            return "LED";
        case Type::Stepper:
            return "STEPPER";
        default:
            return "RELAY";
    }
}

bool StageLink::OutputCatalog::addOutput(
    Type type,
    const uint8_t *channels,
    uint8_t channelCount,
    const char *defaultName
)
{
    if (count_ >= MAX_OUTPUTS
        || channels == nullptr
        || channelCount == 0
        || channelCount > MAX_CHANNELS_PER_OUTPUT)
    {
        return false;
    }

    Output &output = outputs_[count_];
    output.type = type;
    output.channelCount = channelCount;

    for (uint8_t i = 0; i < channelCount; ++i)
    {
        output.channels[i] = channels[i];
    }

    // A saved name wins over the default, so renaming survives a reboot.
    char key[16];
    buildNameKey(count_, key, sizeof(key));

    bool loaded = false;
    if (ConfigManager::hasKey(key))
    {
        char stored[NAME_SIZE] = {};
        const size_t read = ConfigManager::getBytes(key, stored, sizeof(stored));
        if (read > 0)
        {
            stored[NAME_SIZE - 1] = '\0'; // never trust stored data to be terminated
            snprintf(output.name, NAME_SIZE, "%s", stored);
            loaded = true;
        }
    }

    if (!loaded)
    {
        snprintf(output.name, NAME_SIZE, "%s", defaultName != nullptr ? defaultName : typeName(type));
    }

    count_++;

    return true;
}

uint8_t StageLink::OutputCatalog::getCount() const
{
    return count_;
}

const char *StageLink::OutputCatalog::getName(uint8_t index) const
{
    return index < count_ ? outputs_[index].name : "";
}

StageLink::OutputCatalog::Type StageLink::OutputCatalog::getType(uint8_t index) const
{
    return index < count_ ? outputs_[index].type : Type::Servo;
}

const uint8_t *StageLink::OutputCatalog::getChannels(uint8_t index, uint8_t &countOut) const
{
    if (index >= count_)
    {
        countOut = 0;
        return nullptr;
    }

    countOut = outputs_[index].channelCount;

    return outputs_[index].channels;
}

uint8_t StageLink::OutputCatalog::indexForChannel(uint8_t channel) const
{
    for (uint8_t i = 0; i < count_; ++i)
    {
        for (uint8_t c = 0; c < outputs_[i].channelCount; ++c)
        {
            if (outputs_[i].channels[c] == channel)
            {
                return i;
            }
        }
    }

    return NOT_FOUND;
}

bool StageLink::OutputCatalog::rename(uint8_t index, const char *name)
{
    if (index >= count_ || name == nullptr)
    {
        return false;
    }

    snprintf(outputs_[index].name, NAME_SIZE, "%s", name);

    char key[16];
    buildNameKey(index, key, sizeof(key));
    ConfigManager::putBytes(key, outputs_[index].name, NAME_SIZE);

    return true;
}
