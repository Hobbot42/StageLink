#include "StatusPageCycler.h"

// StageLink StatusPageCycler
// Belongs to: StageLink_Common (shared by TX and RX).

void StageLink::StatusPageCycler::begin(uint8_t pageCount)
{
    this->pageCount = pageCount > 0 ? pageCount : 1;
    currentPage = 0;
    lastAdvanceTime = millis();
}

void StageLink::StatusPageCycler::next()
{
    currentPage = (currentPage + 1) % pageCount;
    lastAdvanceTime = millis();
}

// Optional time-based paging (not currently used by TX/RX, which cycle
// pages on button press instead) - kept available for boards that want
// pages to rotate automatically.
bool StageLink::StatusPageCycler::autoAdvance(unsigned long intervalMs)
{
    if (millis() - lastAdvanceTime < intervalMs)
    {
        return false;
    }

    next();
    return true;
}

uint8_t StageLink::StatusPageCycler::page() const
{
    return currentPage;
}
