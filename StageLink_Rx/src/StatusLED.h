// StageLink StatusLED
// Single-pixel NeoPixel giving an at-a-glance link-state indicator:
// green = peer connected, red = offline/waiting, blue = config mode.
// Belongs to: StageLink_Rx (an identical copy also exists in
// StageLink_Tx - kept duplicated rather than shared since each board
// wires its own LED_PIN and the class has no other dependencies).

#pragma once

class StatusLED
{
public:

    static void begin();

    static void setReady();

    static void setOffline();

    static void setConfig();
};