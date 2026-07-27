#include "ShowStorage.h"

#include <cstdio>
#include <cstring>
#include "ConfigManager.h"

namespace
{
    // NVS caps keys at 15 characters (see EffectStorage.cpp) - these are
    // well under even at their longest ("show15" = 6).
    constexpr size_t KEY_BUFFER_SIZE = 16;

    constexpr const char *KEY_VERSION = "shvers";
    constexpr const char *KEY_RECORD_SIZE = "shrecsz";
    constexpr const char *KEY_COUNT = "shcount";

    // Guards a corrupt or hostile stored size from being used as a length
    // for a read into a fixed buffer. Comfortably above one show.
    constexpr size_t MAX_REASONABLE_RECORD_SIZE = 4096;

    void buildRecordKey(uint8_t index, char *buffer, size_t bufferSize)
    {
        snprintf(buffer, bufferSize, "show%u", static_cast<unsigned>(index));
    }

    const uint8_t *recordAt(const void *records, size_t recordSize, uint8_t index)
    {
        return static_cast<const uint8_t *>(records) + static_cast<size_t>(index) * recordSize;
    }

    uint8_t *recordAt(void *records, size_t recordSize, uint8_t index)
    {
        return static_cast<uint8_t *>(records) + static_cast<size_t>(index) * recordSize;
    }
}

bool StageLink::ShowStorage::save(const void *records, size_t recordSize, uint8_t recordCount)
{
    if (records == nullptr || recordSize == 0 || recordSize > MAX_REASONABLE_RECORD_SIZE)
    {
        return false;
    }

    bool allWritten = true;

    for (uint8_t i = 0; i < recordCount; ++i)
    {
        char key[KEY_BUFFER_SIZE];
        buildRecordKey(i, key, sizeof(key));

        const size_t written =
            ConfigManager::putBytes(key, recordAt(records, recordSize, i), recordSize);

        if (written != recordSize)
        {
            allWritten = false;
        }
    }

    // The header goes last: until it is written the records aren't
    // considered valid, so a power loss midway through leaves the
    // previous save intact rather than a half-written list.
    ConfigManager::putUInt8(KEY_COUNT, recordCount);
    ConfigManager::putInt(KEY_RECORD_SIZE, static_cast<int32_t>(recordSize));
    ConfigManager::putInt(KEY_VERSION, static_cast<int32_t>(LAYOUT_VERSION));

    return allWritten;
}

bool StageLink::ShowStorage::load(
    void *records, size_t recordSize, uint8_t maxRecords, uint8_t &countOut
)
{
    countOut = 0;

    if (records == nullptr || recordSize == 0)
    {
        return false;
    }

    if (!ConfigManager::hasKey(KEY_VERSION))
    {
        return false; // nothing saved yet - a first boot, not an error
    }

    const int32_t storedVersion = ConfigManager::getInt(KEY_VERSION, -1);
    const int32_t storedRecordSize = ConfigManager::getInt(KEY_RECORD_SIZE, -1);

    // Both must agree with this firmware. See ShowStorage.h on why a
    // mismatch discards rather than attempts a best-effort read.
    if (storedVersion != static_cast<int32_t>(LAYOUT_VERSION)
        || storedRecordSize != static_cast<int32_t>(recordSize))
    {
        return false;
    }

    uint8_t storedCount = ConfigManager::getUInt8(KEY_COUNT, 0);
    if (storedCount > maxRecords)
    {
        storedCount = maxRecords;
    }

    for (uint8_t i = 0; i < storedCount; ++i)
    {
        char key[KEY_BUFFER_SIZE];
        buildRecordKey(i, key, sizeof(key));

        const size_t read =
            ConfigManager::getBytes(key, recordAt(records, recordSize, i), recordSize);

        if (read != recordSize)
        {
            // A short record means the list is not what the header
            // claims, so stop and keep only what read cleanly.
            countOut = i;
            return i > 0;
        }
    }

    countOut = storedCount;
    return true;
}

void StageLink::ShowStorage::clear()
{
    // The header first - that alone makes load() report nothing saved,
    // so an interrupted clear can't leave records looking valid.
    ConfigManager::removeKey(KEY_VERSION);
    ConfigManager::removeKey(KEY_RECORD_SIZE);
    ConfigManager::removeKey(KEY_COUNT);

    for (uint8_t i = 0; i < 255; ++i)
    {
        char key[KEY_BUFFER_SIZE];
        buildRecordKey(i, key, sizeof(key));

        if (!ConfigManager::hasKey(key))
        {
            break;
        }

        ConfigManager::removeKey(key);
    }
}
