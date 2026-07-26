// StageLink_Rx UpdateMode
// Reached from Setup > Update Mode (see GuiController.h/main.cpp) -
// brings up WiFi station mode + ArduinoOTA so firmware can be pushed
// from PlatformIO, without that WiFi stack running during normal
// ESP-NOW operation. Entering/exiting always goes through a reboot
// (ESP.restart()) rather than tearing WiFi back down in place, so
// normal boot's radio/ShowEngine/ActionEngine setup is always started
// from a clean slate - see main.cpp's setup().
// Belongs to: StageLink_Rx.

#pragma once

class UpdateMode
{
public:
    // Starts WiFi.begin() with the credentials in secrets.h and shows
    // the "connecting" screen. Non-blocking - update() drives the rest
    // of the connect/OTA-wait/flash sequence.
    static void begin();

    // Must be called every loop() while isActive() is true, in place of
    // the normal loop() body (see main.cpp) - pumps ArduinoOTA.handle(),
    // refreshes the OLED with connection/IP status, and reboots via
    // ESP.restart() if 2 minutes pass with no OTA flash actually
    // starting (see ArduinoOTA's onStart callback in UpdateMode.cpp).
    static void update();

    static bool isActive();
};
