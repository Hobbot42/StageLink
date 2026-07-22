// StageLink LEDOutput
// Drives one addressable LED strip as a single RGB light - every pixel
// set to the same color. Maintains Red/Green/Blue/Brightness as internal
// state (see setRed/setGreen/setBlue/setBrightness); each setter updates
// a target value instantly, and tick() (called every loop(), see RX
// main.cpp) eases the actual displayed value toward that target a step
// at a time, so a value change fades in rather than snapping instantly -
// bigger jumps naturally take longer since they need more steps.
//
// OutputManager still only knows "one channel -> one OutputDevice", so
// LEDOutput's four logical channels are exposed as four separate
// registrations (see RX main.cpp): LEDOutput itself handles brightness
// directly via the OutputDevice::update() it's registered with, and
// three small LEDChannelProxy adapters (below) forward their channel's
// update() into LEDOutput's setRed/setGreen/setBlue. This keeps
// OutputManager and the OutputDevice interface completely unchanged
// while still letting four independent channels drive one shared light.
//
// Supports multiple underlying wire protocols (WS2812 one-wire,
// APA102/SK9822 clocked) selected at runtime via ConfigManager - see
// LEDProtocol and reloadConfiguration(). The concrete per-protocol driver
// (LEDStripDriver and its implementations) is an internal detail of
// LEDOutput.cpp; nothing outside this file needs to know which protocol
// is in use, and a future settings menu can change protocol/pins/count
// by writing ConfigManager and calling reloadConfiguration() - no other
// code needs to change.
//
// Deliberately minimal for this first version: no white channel, per-
// pixel addressing, effects, presets, or WLED/DMX passthrough yet. Those
// are meant to come later, either as new methods here or as new
// OutputDevice implementations registered on their own channels.
// Belongs to: StageLink_Rx.

#pragma once

#include <cstdint>
#include "OutputDevice.h"

enum class LEDProtocol : uint8_t
{
    WS2812 = 0,
    APA102 = 1,
    SK9822 = 2,
    Auto = 3 // reserved - protocol auto-detection not yet implemented
};

// Defined in LEDOutput.cpp - an implementation detail, not part of this
// class's public surface.
class LEDStripDriver;

class LEDOutput : public StageLink::OutputDevice
{
public:
    ~LEDOutput() override;

    bool begin() override;

    // Registered as the brightness channel's handler - equivalent to
    // calling setBrightness(). See the class comment for why brightness
    // specifically goes through the OutputDevice interface directly
    // while red/green/blue go through LEDChannelProxy instead.
    void update(int32_t value) override;

    void diagnostics() override;

    // Re-reads protocol/pins/count/brightness from ConfigManager and
    // (re)builds the underlying driver accordingly. begin() just calls
    // this once at startup - a future settings menu can call it again
    // after writing new ConfigManager values, with no other changes
    // needed here.
    bool reloadConfiguration();

    // Each 0-255 (clamped). Sets the target for that channel - see the
    // class comment. The physical strip doesn't change until tick()
    // steps toward it.
    void setRed(int32_t value);
    void setGreen(int32_t value);
    void setBlue(int32_t value);
    void setBrightness(int32_t value);

    // Call every loop(). Rate-limited internally - a no-op most calls,
    // only actually steps (and refreshes the strip) once per
    // FADE_STEP_INTERVAL_MS.
    void tick();

private:
    void refresh();

    LEDProtocol protocol = LEDProtocol::WS2812;
    uint8_t dataPin = 0;
    uint8_t clockPin = 0;
    uint16_t ledCount = 0;
    LEDStripDriver *driver = nullptr;

    // Currently displayed - what refresh() actually sends to the strip.
    uint8_t red = 255;
    uint8_t green = 255;
    uint8_t blue = 255;
    uint8_t brightness = 0;

    // Where tick() is easing the displayed values toward.
    uint8_t targetRed = 255;
    uint8_t targetGreen = 255;
    uint8_t targetBlue = 255;
    uint8_t targetBrightness = 0;

    unsigned long lastFadeStepTime = 0;
};

// Thin OutputDevice adapter that forwards update() to one channel setter
// on a shared LEDOutput. See the LEDOutput class comment for why this
// exists - lets OutputManager's one-channel-per-device model address
// LEDOutput's Red/Green/Blue channels individually.
class LEDChannelProxy : public StageLink::OutputDevice
{
public:
    enum class Channel : uint8_t
    {
        Red,
        Green,
        Blue
    };

    LEDChannelProxy(LEDOutput &owner, Channel channel);

    // No-op - the owner's own registration handles real hardware init.
    bool begin() override;

    void update(int32_t value) override;

    void diagnostics() override;

private:
    LEDOutput &owner;
    Channel channel;
};
