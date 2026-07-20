#pragma once

#include <Arduino.h>

namespace StageLink
{
    // Tracks which status page a display is showing. Generic and
    // hardware-agnostic so any node can add more pages later without
    // rebuilding the paging logic.
    class StatusPageCycler
    {
    public:
        void begin(uint8_t pageCount);

        void next();

        bool autoAdvance(unsigned long intervalMs);

        uint8_t page() const;

    private:
        uint8_t pageCount = 1;
        uint8_t currentPage = 0;
        unsigned long lastAdvanceTime = 0;
    };
}
