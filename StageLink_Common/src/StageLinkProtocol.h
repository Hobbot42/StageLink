#pragma once

#include <Arduino.h>

namespace StageLink
{
    constexpr size_t MAX_PAYLOAD_SIZE = 48;

    enum class PacketType : uint8_t
    {
        Heartbeat = 1,
        Cue = 2,
        Servo = 3,
        Dmx = 4,
        Configuration = 5,
        BUTTON_EVENT = 7,
        STATE_REQUEST = 8,
        STATE_SNAPSHOT = 9,
        VALUE_UPDATE = 10,
        Acknowledgement = 255
    };

    enum class ButtonState : uint8_t
    {
        Released = 0,
        Pressed = 1
    };

    // Identifies which control a VALUE_UPDATE packet carries. Add new
    // entries here as future analog/control features come online
    // (servo, digital outputs, stepper, etc.) instead of adding new
    // packet types.
    enum class ValueChannel : uint8_t
    {
        Encoder = 0
    };

    constexpr uint8_t VALUE_UPDATE_PAYLOAD_SIZE = 5;

    inline void encodeValueUpdate(
        ValueChannel channel,
        int32_t value,
        uint8_t *payload
    )
    {
        uint32_t raw = static_cast<uint32_t>(value);

        payload[0] = static_cast<uint8_t>(channel);
        payload[1] = raw & 0xFF;
        payload[2] = (raw >> 8) & 0xFF;
        payload[3] = (raw >> 16) & 0xFF;
        payload[4] = (raw >> 24) & 0xFF;
    }

    inline ValueChannel decodeValueUpdateChannel(const char *payload)
    {
        return static_cast<ValueChannel>(static_cast<uint8_t>(payload[0]));
    }

    inline int32_t decodeValueUpdateValue(const char *payload)
    {
        uint32_t raw =
            static_cast<uint8_t>(payload[1]) |
            (static_cast<uint32_t>(static_cast<uint8_t>(payload[2])) << 8) |
            (static_cast<uint32_t>(static_cast<uint8_t>(payload[3])) << 16) |
            (static_cast<uint32_t>(static_cast<uint8_t>(payload[4])) << 24);

        return static_cast<int32_t>(raw);
    }

    struct __attribute__((packed)) StagePacket
    {
        PacketType type;
        uint16_t sequence;
        uint16_t sessionId;
        uint16_t acknowledgedSequence;
        uint16_t acknowledgedSessionId;
        uint8_t payloadLength;
        char payload[MAX_PAYLOAD_SIZE];
    };

    using Packet = StagePacket;

    inline const char *packetTypeName(PacketType type)
    {
        switch (type)
        {
            case PacketType::Heartbeat:
                return "heartbeat";
            case PacketType::Cue:
                return "cue";
            case PacketType::Servo:
                return "servo";
            case PacketType::Dmx:
                return "DMX";
            case PacketType::Configuration:
                return "configuration";
            case PacketType::BUTTON_EVENT:
                return "button";
            case PacketType::STATE_REQUEST:
                return "state request";
            case PacketType::STATE_SNAPSHOT:
                return "state snapshot";
            case PacketType::VALUE_UPDATE:
                return "value update";
            case PacketType::Acknowledgement:
                return "acknowledgement";
            default:
                return "unknown";
        }
    }
}
