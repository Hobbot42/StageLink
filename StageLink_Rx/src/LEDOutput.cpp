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

    // How often (and how far) tick() steps a channel toward its target.
    // Duration scales with distance: a full 0-255 sweep takes ~1.3s
    // (128 steps * 10ms), a single 5-unit click-step takes ~30ms.
    constexpr unsigned long FADE_STEP_INTERVAL_MS = 10;
    constexpr uint8_t FADE_STEP_SIZE = 2;

    // Moves current one step toward target (clamped so it never
    // overshoots). Returns whether it actually changed.
    bool stepToward(uint8_t &current, uint8_t target, uint8_t step)
    {
        if (current == target)
        {
            return false;
        }

        int delta = static_cast<int>(target) - static_cast<int>(current);
        int clampedStep = constrain(delta, -static_cast<int>(step), static_cast<int>(step));
        current = static_cast<uint8_t>(static_cast<int>(current) + clampedStep);

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
    if (millis() - lastFadeStepTime < FADE_STEP_INTERVAL_MS)
    {
        return;
    }

    lastFadeStepTime = millis();

    bool changed = false;
    changed |= stepToward(red, targetRed, FADE_STEP_SIZE);
    changed |= stepToward(green, targetGreen, FADE_STEP_SIZE);
    changed |= stepToward(blue, targetBlue, FADE_STEP_SIZE);
    changed |= stepToward(brightness, targetBrightness, FADE_STEP_SIZE);

    if (changed)
    {
        refresh();
    }
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
