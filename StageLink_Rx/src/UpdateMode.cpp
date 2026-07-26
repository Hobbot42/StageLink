#include "UpdateMode.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "Display.h"
#include "secrets.h"

namespace
{
    // Covers WiFi association *and* waiting for an OTA push to actually
    // start - reset back to full whenever ArduinoOTA's onError fires, so
    // a failed attempt still gets a fresh window to retry rather than
    // rebooting mid-retry. Not enforced once a flash is in progress (see
    // State::Flashing below) - a slow upload should never itself trigger
    // a reboot.
    constexpr unsigned long TIMEOUT_MS = 120000; // 2 minutes
    constexpr unsigned long DISPLAY_REFRESH_INTERVAL_MS = 500;

    enum class State
    {
        Connecting,
        WaitingForOta,
        Flashing
    };

    bool active = false;
    State state = State::Connecting;
    unsigned long deadlineStart = 0;
    unsigned long lastDisplayRefresh = 0;

    void onOtaStart()
    {
        state = State::Flashing;
    }

    void onOtaEnd()
    {
        // ArduinoOTA's own success path already reboots once the new
        // image is written; this is a defensive fallback in case that
        // ever changes, not the primary reboot path.
        ESP.restart();
    }

    void onOtaError(ota_error_t)
    {
        // Give the operator a fresh timeout window to retry rather than
        // counting the failed attempt against the original one.
        state = State::WaitingForOta;
        deadlineStart = millis();
    }

    void refreshDisplay()
    {
        const char *statusLine = "Connecting...";
        if (state == State::WaitingForOta)
        {
            statusLine = "Waiting for OTA";
        }
        else if (state == State::Flashing)
        {
            statusLine = "Flashing...";
        }

        bool hasIp = WiFi.status() == WL_CONNECTED;
        int secondsRemaining = -1;

        if (state != State::Flashing)
        {
            unsigned long elapsed = millis() - deadlineStart;
            unsigned long remainingMs = elapsed < TIMEOUT_MS ? TIMEOUT_MS - elapsed : 0;
            secondsRemaining = static_cast<int>(remainingMs / 1000);
        }

        Display::showUpdateMode(
            statusLine,
            hasIp ? WiFi.localIP().toString().c_str() : nullptr,
            secondsRemaining
        );
    }
}

void UpdateMode::begin()
{
    active = true;
    state = State::Connecting;
    deadlineStart = millis();

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    ArduinoOTA.setHostname("stagelink-rx");
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart(onOtaStart);
    ArduinoOTA.onEnd(onOtaEnd);
    ArduinoOTA.onError(onOtaError);

    refreshDisplay();
}

void UpdateMode::update()
{
    if (!active)
    {
        return;
    }

    if (state == State::Connecting && WiFi.status() == WL_CONNECTED)
    {
        ArduinoOTA.begin();
        state = State::WaitingForOta;
        deadlineStart = millis();
    }

    if (state != State::Connecting)
    {
        ArduinoOTA.handle();
    }

    // Only Connecting/WaitingForOta are subject to the timeout - once a
    // flash actually starts (State::Flashing) the operator is committed,
    // and onOtaEnd()/onOtaError() are what move things along from there.
    if (state != State::Flashing && millis() - deadlineStart >= TIMEOUT_MS)
    {
        ESP.restart();
    }

    if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_INTERVAL_MS)
    {
        refreshDisplay();
        lastDisplayRefresh = millis();
    }
}

bool UpdateMode::isActive()
{
    return active;
}
