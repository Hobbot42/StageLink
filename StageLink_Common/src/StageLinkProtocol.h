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
        ENCODER_VALUE = 6,
        BUTTON_EVENT = 7,
        STATE_REQUEST = 8,
        STATE_SNAPSHOT = 9,
        Acknowledgement = 255
    };

    enum class ButtonState : uint8_t
    {
        Released = 0,
        Pressed = 1
    };

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
            case PacketType::ENCODER_VALUE:
                return "encoder";
            case PacketType::BUTTON_EVENT:
                return "button";
            case PacketType::STATE_REQUEST:
                return "state request";
            case PacketType::STATE_SNAPSHOT:
                return "state snapshot";
            case PacketType::Acknowledgement:
                return "acknowledgement";
            default:
                return "unknown";
        }
    }
}
