// StageLink RxQ ShowStorage
// Keeps the show list in flash so programming survives a power cycle.
// Deliberately knows nothing about what a show contains: ShowEngine hands
// it an array of fixed-size records and gets the same bytes back, so the
// show/cue/action layout can change without touching this file.
//
// Two guards stop a layout change from being read back as valid show
// data. LAYOUT_VERSION is stamped alongside the records and must be
// bumped whenever that layout changes; the record size is stored too and
// checked independently, which catches a struct that grew while someone
// forgot to bump the version. A mismatch on either makes load() report
// "nothing saved" rather than reinterpreting old bytes as cues - the
// operator loses saved shows, which is far better than a show that
// executes garbage on real hardware.
//
// Stored as one file on the LittleFS partition rather than in NVS. NVS
// is only 20KB and shared with WiFi calibration and every setting, which
// capped the whole show list at a few kilobytes; the data partition in
// the standard ESP32 table is 1.4MB and was otherwise unused. The
// partition table itself is unchanged, so OTA still works.
// Belongs to: StageLink_Rx - shows live on the RxQ (see CLAUDE.md).

#pragma once

#include <cstddef>
#include <cstdint>

namespace StageLink
{
    class ShowStorage
    {
    public:
        // Bump whenever the stored show/cue/action layout changes. See
        // the file comment for what a mismatch does.
        static constexpr uint16_t LAYOUT_VERSION = 4;

        // Mounts the filesystem. Call once at startup, before load().
        static void begin();

        // Writes recordCount records of recordSize bytes each, plus the
        // magic/version/size/count header used to validate them on load.
        // Written to a temporary file and renamed into place, so an
        // interrupted save leaves the previous one intact. Returns false
        // if the write fails.
        static bool save(const void *records, size_t recordSize, uint8_t recordCount);

        // Fills up to maxRecords records and reports how many were read.
        // Returns false - leaving countOut at 0 and records untouched -
        // when nothing is stored, or when what is stored doesn't match
        // this firmware's layout.
        static bool load(void *records, size_t recordSize, uint8_t maxRecords, uint8_t &countOut);

        // Forgets everything, so the next load() reports nothing saved.
        static void clear();
    };
}
