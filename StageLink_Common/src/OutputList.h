// StageLink OutputList
// Enumerates RX's actual output *instances* - e.g. "there is one Motion
// output at index 0 and one LedAddressable output at index 0" - rather
// than just which capability types are present (see DeviceInfo.h's
// capability bitmask, which stays as-is; this is additive, a finer-
// grained layer on top). Carried by OUTPUT_LIST_REQUEST/
// OUTPUT_LIST_RESPONSE packets, same request/reply shape as
// CAPABILITY_REQUEST/CAPABILITY_RESPONSE.
//
// Deliberately minimal for this first version: no names, ranges, min/
// max, default values, or writable/read-only flags yet. OutputDescriptor
// carries two reserved bytes so those can be added later by giving them
// meaning, without changing the wire size of capability/index or
// breaking older firmware that only reads the fields it knows about.
// Belongs to: StageLink_Common (shared by TX and RX).

#pragma once

#include <Arduino.h>
#include "DeviceInfo.h"

namespace StageLink
{
    struct OutputDescriptor
    {
        DeviceCapability capability; // what kind of output this is
        uint8_t index;               // which instance of that capability (0-based)
        uint8_t reserved[2];         // future flags/ranges - always 0 for now
    };

    constexpr uint8_t MAX_OUTPUT_DESCRIPTORS = 16;
    constexpr uint8_t OUTPUT_DESCRIPTOR_WIRE_SIZE = 4; // capability(1) + index(1) + reserved(2)
    constexpr uint8_t OUTPUT_LIST_HEADER_SIZE = 1;     // count
    constexpr uint8_t MAX_OUTPUT_LIST_PAYLOAD_SIZE =
        OUTPUT_LIST_HEADER_SIZE + (MAX_OUTPUT_DESCRIPTORS * OUTPUT_DESCRIPTOR_WIRE_SIZE);

    struct OutputList
    {
        OutputDescriptor outputs[MAX_OUTPUT_DESCRIPTORS];
        uint8_t count;
    };

    inline void clearOutputList(OutputList &list)
    {
        list.count = 0;
    }

    // Appends one descriptor. This is how a sender builds a list without
    // needing to know indices - same pattern as
    // StateSnapshot.h's addOrUpdateStateItem, minus the upsert-by-key
    // behavior (outputs don't change identity at runtime, so plain
    // append is enough).
    inline bool addOutputDescriptor(
        OutputList &list,
        DeviceCapability capability,
        uint8_t index
    )
    {
        if (list.count >= MAX_OUTPUT_DESCRIPTORS)
        {
            return false;
        }

        list.outputs[list.count].capability = capability;
        list.outputs[list.count].index = index;
        list.outputs[list.count].reserved[0] = 0;
        list.outputs[list.count].reserved[1] = 0;
        list.count++;

        return true;
    }

    // Writes only the active descriptors to the wire (never the full
    // padded array) - same convention as serializeStateSnapshot.
    inline uint8_t serializeOutputList(const OutputList &list, uint8_t *payload)
    {
        payload[0] = list.count;

        for (uint8_t i = 0; i < list.count; ++i)
        {
            uint8_t offset = OUTPUT_LIST_HEADER_SIZE + (i * OUTPUT_DESCRIPTOR_WIRE_SIZE);

            payload[offset] = static_cast<uint8_t>(list.outputs[i].capability);
            payload[offset + 1] = list.outputs[i].index;
            payload[offset + 2] = list.outputs[i].reserved[0];
            payload[offset + 3] = list.outputs[i].reserved[1];
        }

        return OUTPUT_LIST_HEADER_SIZE + (list.count * OUTPUT_DESCRIPTOR_WIRE_SIZE);
    }

    // Validates payloadLength against the declared count before trusting
    // any of it, so a corrupt or unexpected packet is rejected rather
    // than read out of bounds - same convention as
    // deserializeStateSnapshot.
    inline bool deserializeOutputList(
        const char *payload,
        uint8_t payloadLength,
        OutputList &outList
    )
    {
        if (payloadLength < OUTPUT_LIST_HEADER_SIZE)
        {
            return false;
        }

        uint8_t count = static_cast<uint8_t>(payload[0]);

        if (count > MAX_OUTPUT_DESCRIPTORS)
        {
            return false;
        }

        if (payloadLength != OUTPUT_LIST_HEADER_SIZE + (count * OUTPUT_DESCRIPTOR_WIRE_SIZE))
        {
            return false;
        }

        outList.count = count;

        for (uint8_t i = 0; i < count; ++i)
        {
            uint8_t offset = OUTPUT_LIST_HEADER_SIZE + (i * OUTPUT_DESCRIPTOR_WIRE_SIZE);

            outList.outputs[i].capability =
                static_cast<DeviceCapability>(static_cast<uint8_t>(payload[offset]));
            outList.outputs[i].index = static_cast<uint8_t>(payload[offset + 1]);
            outList.outputs[i].reserved[0] = static_cast<uint8_t>(payload[offset + 2]);
            outList.outputs[i].reserved[1] = static_cast<uint8_t>(payload[offset + 3]);
        }

        return true;
    }
}
