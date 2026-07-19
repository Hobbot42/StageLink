#pragma once

class StatusLED
{
public:

    static void begin();

    static void setReady();

    static void setOffline();

    static void setConfig();
};