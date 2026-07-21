#pragma once

#include <cstdint>

// StageLink OutputDevice
// Common lifecycle interface every physical output (servo today; future
// DMX, stepper, digital/analog outputs, ...) implements, so
// OutputManager can initialize, route to, and (eventually) query any of
// them generically.
// Belongs to: StageLink_Common (shared by TX and RX).

namespace StageLink
{
    class OutputDevice
    {
    public:
        virtual ~OutputDevice() = default;

        // Initializes the underlying hardware and loads any device config
        // (e.g. from ConfigManager). Any device-specific config (pin, etc.)
        // is supplied by the concrete implementation's own constructor, not
        // this interface - begin() itself takes nothing so OutputManager
        // can call it uniformly across device types. Returns false if
        // initialization failed, so callers (OutputManager::registerDevice)
        // can report it rather than silently proceeding as if the device
        // were ready.
        virtual bool begin() = 0;

        // Applies one new value for this device's channel.
        virtual void update(int32_t value) = 0;

        // Placeholder for future device status reporting (fault flags,
        // current position, temperature, ...). No shape defined yet -
        // deliberately left minimal until a real device needs it.
        virtual void diagnostics() = 0;
    };
}
