// StageLink Button
// Dedicated cue-trigger button on TX, separate from the encoder's
// built-in button - pressing it sends a Cue packet (see TX main.cpp).
// Belongs to: StageLink_Tx.

#pragma once

class Button
{
public:

    static void begin();

    // Live (debounced by hardware pull-up + simple level read) pressed state.
    static bool pressed();
};