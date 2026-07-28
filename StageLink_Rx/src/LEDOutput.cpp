#include "LEDOutput.h"

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_DotStar.h>
#include "ConfigManager.h"

// Internal driver interface - LEDOutput only ever talks to this, never
// to Adafruit_NeoPixel/Adafruit_DotStar directly. Adding a new protocol
// later means adding one more class below plus a case in createDriver(),
// not touching LEDOutput's own logic.
class LEDStripDriver
{
public:
    virtual ~LEDStripDriver() = default;
    virtual bool begin(uint8_t dataPin, uint8_t clockPin, uint16_t count) = 0;
    virtual void setBrightness(uint8_t brightness) = 0;
    virtual void fillColor(uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void show() = 0;
};

namespace
{
    // Fallback values used until these keys are ever written - defaults
    // match the clocked (DI/CI) strip currently wired to RX: OUT-03
    // (GPIO18/19). Data/clock order (18=data, 19=clock) is assumed, not
    // separately confirmed - if the strip doesn't respond, try swapping
    // these two values first.
    constexpr LEDProtocol DEFAULT_LED_PROTOCOL = LEDProtocol::APA102;
    constexpr uint8_t DEFAULT_DATA_PIN = 18;
    constexpr uint8_t DEFAULT_CLOCK_PIN = 19;
    constexpr int32_t DEFAULT_LED_COUNT = 8;

    // Takes the commanded value as-is. This used to ease toward the
    // target a couple of units at a time, which added a fixed ~1.3s
    // fade to every change - that made a zero-second cue visibly fade,
    // and made a timed cue land late, since the strip was still catching
    // up after ActionEngine's ramp had finished. Timing belongs to
    // ActionEngine now (see ActionEngine.h), so there is exactly one
    // place that decides how long a change takes.
    bool applyNow(uint8_t &current, uint8_t target)
    {
        if (current == target)
        {
            return false;
        }

        current = target;

        return true;
    }

    // One-wire (WS2812/NeoPixel-class). clockPin is unused.
    class WS2812Driver : public LEDStripDriver
    {
    public:
        bool begin(uint8_t dataPin, uint8_t /*clockPin*/, uint16_t count) override
        {
            strip.setPin(dataPin);
            strip.updateLength(count);
            return strip.begin();
        }

        void setBrightness(uint8_t brightness) override
        {
            strip.setBrightness(brightness);
        }

        void fillColor(uint8_t r, uint8_t g, uint8_t b) override
        {
            strip.fill(strip.Color(r, g, b));
        }

        void show() override
        {
            strip.show();
        }

    private:
        Adafruit_NeoPixel strip;
    };

    // Clocked (APA102/SK9822-class). Both chips share the same control
    // protocol, so one driver covers both LEDProtocol values. Uses
    // Adafruit_DotStar's "soft SPI" mode, which allows any two GPIOs
    // rather than requiring the hardware SPI pins.
    class APA102Driver : public LEDStripDriver
    {
    public:
        // Adafruit_DotStar has no no-arg constructor - the pin/count
        // values here are throwaway, replaced by begin()'s updatePins()/
        // updateLength() before anything is driven. Color order (BGR)
        // isn't reconfigurable after construction, so it's set here for
        // real - confirmed against actual hardware (default BRG had red
        // and green swapped).
        APA102Driver() : strip(1, 0, 1, DOTSTAR_BGR) {}

        bool begin(uint8_t dataPin, uint8_t clockPin, uint16_t count) override
        {
            strip.updatePins(dataPin, clockPin);
            strip.updateLength(count);
            strip.begin();
            return true;
        }

        void setBrightness(uint8_t brightness) override
        {
            strip.setBrightness(brightness);
        }

        void fillColor(uint8_t r, uint8_t g, uint8_t b) override
        {
            strip.fill(strip.Color(r, g, b));
        }

        void show() override
        {
            strip.show();
        }

    private:
        Adafruit_DotStar strip;
    };

    LEDStripDriver *createDriver(LEDProtocol protocol)
    {
        switch (protocol)
        {
            case LEDProtocol::APA102:
            case LEDProtocol::SK9822:
                return new APA102Driver();
            case LEDProtocol::WS2812:
            case LEDProtocol::Auto: // reserved - not yet implemented, falls back to WS2812
            default:
                return new WS2812Driver();
        }
    }
}

LEDOutput::~LEDOutput()
{
    delete driver;
}

bool LEDOutput::begin()
{
    return reloadConfiguration();
}

bool LEDOutput::reloadConfiguration()
{
    delete driver;
    driver = nullptr;

    protocol = static_cast<LEDProtocol>(
        StageLink::ConfigManager::getUInt8(
            "ledProtocol",
            static_cast<uint8_t>(DEFAULT_LED_PROTOCOL)
        )
    );
    dataPin = StageLink::ConfigManager::getUInt8("ledDataPin", DEFAULT_DATA_PIN);
    clockPin = StageLink::ConfigManager::getUInt8("ledClockPin", DEFAULT_CLOCK_PIN);
    ledCount = static_cast<uint16_t>(
        StageLink::ConfigManager::getInt("ledCount", DEFAULT_LED_COUNT)
    );

    driver = createDriver(protocol);
    bool success = driver->begin(dataPin, clockPin, ledCount);

    // Safe startup state: fully off. Hardcoded, not read from
    // ConfigManager or left to whatever the driver library defaults to -
    // hardware testing showed the strip briefly flashing at full/white
    // brightness on RX power-up, before the first StateSnapshot arrived
    // to set the real commanded value. Current and target are set
    // together (not faded) so reload itself never becomes visible; the
    // strip stays dark until a real command (VALUE_UPDATE or
    // StateSnapshot) moves the target, at which point tick() fades it in
    // exactly as it fades any other live change.
    red = targetRed = 0;
    green = targetGreen = 0;
    blue = targetBlue = 0;
    brightness = targetBrightness = 0;
    refresh();

    return success;
}

void LEDOutput::refresh()
{
    if (driver == nullptr)
    {
        return;
    }

    driver->fillColor(red, green, blue);
    driver->setBrightness(brightness);
    driver->show();
}

void LEDOutput::setRed(int32_t value)
{
    targetRed = static_cast<uint8_t>(constrain(value, 0, 255));
}

void LEDOutput::setGreen(int32_t value)
{
    targetGreen = static_cast<uint8_t>(constrain(value, 0, 255));
}

void LEDOutput::setBlue(int32_t value)
{
    targetBlue = static_cast<uint8_t>(constrain(value, 0, 255));
}

void LEDOutput::setBrightness(int32_t value)
{
    targetBrightness = static_cast<uint8_t>(constrain(value, 0, 255));
}

void LEDOutput::update(int32_t value)
{
    setBrightness(value);
}

void LEDOutput::tick()
{
    // Values are applied as commanded, with no easing of its own - see
    // the class comment. All this does is push a changed set of values to
    // the strip once per loop, rather than once per setter, so a cue that
    // sets red, green, blue and brightness costs one strip write.
    bool changed = false;
    changed |= applyNow(red, targetRed);
    changed |= applyNow(green, targetGreen);
    changed |= applyNow(blue, targetBlue);
    changed |= applyNow(brightness, targetBrightness);

    if (changed)
    {
        refresh();
    }
}

void LEDOutput::diagnostics()
{
    // Placeholder - nothing to report yet.
}

void LEDOutput::refreshState()
{
    refresh();
}

LEDChannelProxy::LEDChannelProxy(LEDOutput &owner, Channel channel)
    : owner(owner), channel(channel)
{
}

bool LEDChannelProxy::begin()
{
    return true;
}

void LEDChannelProxy::update(int32_t value)
{
    switch (channel)
    {
        case Channel::Red:
            owner.setRed(value);
            break;
        case Channel::Green:
            owner.setGreen(value);
            break;
        case Channel::Blue:
            owner.setBlue(value);
            break;
    }
}

void LEDChannelProxy::diagnostics()
{
    // Placeholder - nothing to report yet.
}

void LEDChannelProxy::refreshState()
{
    // No-op - see the declaration comment in LEDOutput.h.
}
