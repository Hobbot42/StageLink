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
    // match the clocked (DI/CI) strip currently wired to RX.
    constexpr LEDProtocol DEFAULT_LED_PROTOCOL = LEDProtocol::APA102;
    constexpr uint8_t DEFAULT_DATA_PIN = 4;
    constexpr uint8_t DEFAULT_CLOCK_PIN = 5;
    constexpr int32_t DEFAULT_LED_COUNT = 8;
    constexpr uint8_t DEFAULT_LED_BRIGHTNESS = 128;

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
        // Adafruit_DotStar has no no-arg constructor - these are
        // throwaway values, replaced by begin()'s updatePins()/
        // updateLength() before anything is driven.
        APA102Driver() : strip(1, 0, 1) {}

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

    // Reload starts from white at the configured brightness, same
    // starting look as before RGB support existed - live channel updates
    // (setRed/setGreen/setBlue/setBrightness) take it from there.
    red = 255;
    green = 255;
    blue = 255;
    brightness = StageLink::ConfigManager::getUInt8("ledBrightness", DEFAULT_LED_BRIGHTNESS);
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
    red = static_cast<uint8_t>(constrain(value, 0, 255));
    refresh();
}

void LEDOutput::setGreen(int32_t value)
{
    green = static_cast<uint8_t>(constrain(value, 0, 255));
    refresh();
}

void LEDOutput::setBlue(int32_t value)
{
    blue = static_cast<uint8_t>(constrain(value, 0, 255));
    refresh();
}

void LEDOutput::setBrightness(int32_t value)
{
    brightness = static_cast<uint8_t>(constrain(value, 0, 255));
    refresh();
}

void LEDOutput::update(int32_t value)
{
    setBrightness(value);
}

void LEDOutput::diagnostics()
{
    // Placeholder - nothing to report yet.
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
