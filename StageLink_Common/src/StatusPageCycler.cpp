#include "StatusPageCycler.h"

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
