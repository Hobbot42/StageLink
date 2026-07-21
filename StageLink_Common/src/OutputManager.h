#pragma once

#include <cstdint>
#include "OutputDevice.h"

// StageLink OutputManager
// Generic routing layer between "a channel changed to this value" (from
// either the live VALUE_UPDATE path or a STATE_SNAPSHOT resync) and
// whichever OutputDevice is registered to handle that channel. Callers
// decode packets as they already do; only the last step - "now do
// something with this value" - goes through here instead of calling a
// specific device directly.
// Belongs to: StageLink_Common (shared by TX and RX, and reusable by
// future output-bearing nodes).

namespace StageLink
{
    class OutputManager
    {
    public:
        static constexpr uint8_t MAX_DEVICES = 8;

        // Registers a device for a channel and calls its begin(). The
        // binding is kept either way (so update() still routes even if
        // init reported failure) - the return value only reflects whether
        // the device's own begin() succeeded, so callers can log it.
        // Channel numbering is owned by the caller (application code) -
        // OutputManager itself is agnostic to what a channel number means.
        bool registerDevice(uint8_t channel, OutputDevice *device);

        // Routes value to the device registered for channel. Returns false
        // if no device is registered for that channel (e.g. an unrecognized
        // channel arrived over the air) - not an error, just ignored by the
        // caller.
        bool update(uint8_t channel, int32_t value);

    private:
        struct Binding
        {
            uint8_t channel;
            OutputDevice *device;
        };

        Binding bindings[MAX_DEVICES] = {};
        uint8_t count = 0;
    };
}
