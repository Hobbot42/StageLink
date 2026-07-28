#include "ShowStorage.h"

#include <Arduino.h>
#include <LittleFS.h>

namespace
{
    // The unused data partition in the standard ESP32 table (labelled
    // "spiffs", 1.4MB). Shows used to live in NVS, which is only 20KB and
    // is shared with WiFi calibration and every setting - that capped a
    // show list at a few kilobytes. Nothing about the partition table
    // changes here, so OTA is unaffected.
    constexpr const char *PARTITION_LABEL = "spiffs";
    constexpr const char *MOUNT_POINT = "/littlefs";
    constexpr const char *SHOWS_PATH = "/shows.dat";

    // Guards against reading a file that isn't ours at all.
    constexpr uint32_t FILE_MAGIC = 0x53544753; // "STGS"

    struct Header
    {
        uint32_t magic;
        uint16_t version;
        uint16_t recordSize;
        uint8_t count;
    };

    bool mounted = false;
}

void StageLink::ShowStorage::begin()
{
    // formatOnFail: a first boot, or a partition that was never a
    // filesystem, formats once and comes up empty rather than failing
    // permanently.
    mounted = LittleFS.begin(true, MOUNT_POINT, 10, PARTITION_LABEL);

    if (!mounted)
    {
        Serial.println("SHOW STORAGE: filesystem mount failed - shows will not persist");
    }
}

bool StageLink::ShowStorage::save(const void *records, size_t recordSize, uint8_t recordCount)
{
    if (!mounted || records == nullptr || recordSize == 0)
    {
        return false;
    }

    // Written to a temporary file and renamed over the real one, so a
    // power loss midway through leaves the previous save intact instead
    // of a half-written show list.
    const char *tempPath = "/shows.tmp";

    File file = LittleFS.open(tempPath, "w");
    if (!file)
    {
        return false;
    }

    Header header;
    header.magic = FILE_MAGIC;
    header.version = LAYOUT_VERSION;
    header.recordSize = static_cast<uint16_t>(recordSize);
    header.count = recordCount;

    bool ok = file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header);

    if (ok && recordCount > 0)
    {
        const size_t total = recordSize * static_cast<size_t>(recordCount);
        ok = file.write(static_cast<const uint8_t *>(records), total) == total;
    }

    file.close();

    if (!ok)
    {
        LittleFS.remove(tempPath);
        return false;
    }

    LittleFS.remove(SHOWS_PATH);

    return LittleFS.rename(tempPath, SHOWS_PATH);
}

bool StageLink::ShowStorage::load(
    void *records, size_t recordSize, uint8_t maxRecords, uint8_t &countOut
)
{
    countOut = 0;

    if (!mounted || records == nullptr || recordSize == 0)
    {
        return false;
    }

    File file = LittleFS.open(SHOWS_PATH, "r");
    if (!file)
    {
        return false; // nothing saved yet - a first boot, not an error
    }

    Header header;
    if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header))
    {
        file.close();
        return false;
    }

    // Magic, version and record size all have to agree with this
    // firmware. See ShowStorage.h on why a mismatch discards rather than
    // attempting a best-effort read.
    if (header.magic != FILE_MAGIC
        || header.version != LAYOUT_VERSION
        || header.recordSize != static_cast<uint16_t>(recordSize))
    {
        file.close();
        return false;
    }

    uint8_t storedCount = header.count;
    if (storedCount > maxRecords)
    {
        storedCount = maxRecords;
    }

    for (uint8_t i = 0; i < storedCount; ++i)
    {
        uint8_t *destination = static_cast<uint8_t *>(records) + static_cast<size_t>(i) * recordSize;

        if (file.read(destination, recordSize) != static_cast<int>(recordSize))
        {
            // Short file - keep whatever read cleanly rather than
            // reporting shows that aren't fully there.
            file.close();
            countOut = i;
            return i > 0;
        }
    }

    file.close();
    countOut = storedCount;

    return true;
}

void StageLink::ShowStorage::clear()
{
    if (mounted)
    {
        LittleFS.remove(SHOWS_PATH);
    }
}
