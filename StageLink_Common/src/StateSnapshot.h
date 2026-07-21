// StageLink State Snapshot
// Generic full-state resync container carried by STATE_SNAPSHOT packets.
// Lets TX describe "everything RX needs to know right now" as a list of
// (channel, type, value) items instead of a fixed struct, so new
// devices/channels can be added later without another protocol change.
// Belongs to: StageLink_Common (shared by TX and RX).

#pragma once

#include <Arduino.h>
#include "StageLinkProtocol.h"

namespace StageLink
{
    // Kind of value a StateItem carries. Deliberately separate from
    // ValueChannel (used only by the VALUE_UPDATE live-update path, which
    // conflates "which instance" with "what kind" and is untouched here).
    enum class StateItemType : uint8_t
    {
        Encoder = 0,
        Button = 1,
        Servo = 2,
        Dmx = 3,
        StepperPosition = 4,
        DigitalOutput = 5
    };

    constexpr uint8_t MAX_STATE_ITEMS = 16;
    constexpr uint8_t STATE_ITEM_WIRE_SIZE = 6; // channel(1) + type(1) + value(4, LE)
    constexpr uint8_t STATE_SNAPSHOT_HEADER_SIZE = 1; // itemCount
    constexpr uint8_t MAX_STATE_SNAPSHOT_PAYLOAD_SIZE =
        STATE_SNAPSHOT_HEADER_SIZE + (MAX_STATE_ITEMS * STATE_ITEM_WIRE_SIZE);

    struct StateItem
    {
        uint8_t channel; // free-standing per-instance ID, assigned by application code
        StateItemType type;
        int32_t value;
    };

    struct StateSnapshot
    {
        StateItem items[MAX_STATE_ITEMS];
        uint8_t count;
    };

    inline void clearStateSnapshot(StateSnapshot &snapshot)
    {
        snapshot.count = 0;
    }

    inline uint8_t stateItemCount(const StateSnapshot &snapshot)
    {
        return snapshot.count;
    }

    inline const StateItem *findStateItem(const StateSnapshot &snapshot, uint8_t channel)
    {
        for (uint8_t i = 0; i < snapshot.count; ++i)
        {
            if (snapshot.items[i].channel == channel)
            {
                return &snapshot.items[i];
            }
        }

        return nullptr;
    }

    // Upsert by channel: overwrites in place if this channel is already
    // present, otherwise appends. This is how a sender builds a snapshot
    // without needing to know indices - it just states each channel's
    // current value.
    inline bool addOrUpdateStateItem(
        StateSnapshot &snapshot,
        uint8_t channel,
        StateItemType type,
        int32_t value
    )
    {
        for (uint8_t i = 0; i < snapshot.count; ++i)
        {
            if (snapshot.items[i].channel == channel)
            {
                snapshot.items[i].type = type;
                snapshot.items[i].value = value;
                return true;
            }
        }

        if (snapshot.count >= MAX_STATE_ITEMS)
        {
            return false;
        }

        snapshot.items[snapshot.count].channel = channel;
        snapshot.items[snapshot.count].type = type;
        snapshot.items[snapshot.count].value = value;
        snapshot.count++;

        return true;
    }

    // Writes only the active items to the wire (never the full padded
    // array), so the STATE_SNAPSHOT payload stays small when few channels
    // are in use. Byte layout per item matches encodeValueUpdate's
    // little-endian convention: [channel][type][value LE32].
    inline uint8_t serializeStateSnapshot(const StateSnapshot &snapshot, uint8_t *payload)
    {
        payload[0] = snapshot.count;

        for (uint8_t i = 0; i < snapshot.count; ++i)
        {
            uint8_t offset = STATE_SNAPSHOT_HEADER_SIZE + (i * STATE_ITEM_WIRE_SIZE);
            uint32_t raw = static_cast<uint32_t>(snapshot.items[i].value);

            payload[offset] = snapshot.items[i].channel;
            payload[offset + 1] = static_cast<uint8_t>(snapshot.items[i].type);
            payload[offset + 2] = raw & 0xFF;
            payload[offset + 3] = (raw >> 8) & 0xFF;
            payload[offset + 4] = (raw >> 16) & 0xFF;
            payload[offset + 5] = (raw >> 24) & 0xFF;
        }

        return STATE_SNAPSHOT_HEADER_SIZE + (snapshot.count * STATE_ITEM_WIRE_SIZE);
    }

    // Validates payloadLength against the declared item count before
    // trusting any of it, so a corrupt or unexpected packet is rejected
    // rather than read out of bounds. Unknown StateItemType values are not
    // rejected here - callers decide how to handle them (see RX main.cpp's
    // switch, which safely ignores types it doesn't yet act on).
    inline bool deserializeStateSnapshot(
        const char *payload,
        uint8_t payloadLength,
        StateSnapshot &outSnapshot
    )
    {
        if (payloadLength < STATE_SNAPSHOT_HEADER_SIZE)
        {
            return false;
        }

        uint8_t itemCount = static_cast<uint8_t>(payload[0]);

        if (itemCount > MAX_STATE_ITEMS)
        {
            return false;
        }

        if (payloadLength != STATE_SNAPSHOT_HEADER_SIZE + (itemCount * STATE_ITEM_WIRE_SIZE))
        {
            return false;
        }

        outSnapshot.count = itemCount;

        for (uint8_t i = 0; i < itemCount; ++i)
        {
            uint8_t offset = STATE_SNAPSHOT_HEADER_SIZE + (i * STATE_ITEM_WIRE_SIZE);

            uint32_t raw =
                static_cast<uint8_t>(payload[offset + 2]) |
                (static_cast<uint32_t>(static_cast<uint8_t>(payload[offset + 3])) << 8) |
                (static_cast<uint32_t>(static_cast<uint8_t>(payload[offset + 4])) << 16) |
                (static_cast<uint32_t>(static_cast<uint8_t>(payload[offset + 5])) << 24);

            outSnapshot.items[i].channel = static_cast<uint8_t>(payload[offset]);
            outSnapshot.items[i].type = static_cast<StateItemType>(static_cast<uint8_t>(payload[offset + 1]));
            outSnapshot.items[i].value = static_cast<int32_t>(raw);
        }

        return true;
    }
}
