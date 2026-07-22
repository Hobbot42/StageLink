// StageLink Protocol
// Shared wire format for all TX <-> RX communication.
// Defines the packet types, the fixed StagePacket layout sent over
// ESP-NOW, and the VALUE_UPDATE encode/decode helpers.
// This is the one format both boards must agree on byte-for-byte -
// ReliableRadio rejects any received packet whose size doesn't match
// StagePacket exactly, so TX and RX must always be built and flashed
// from the same version of this file.
// Belongs to: StageLink_Common (shared by TX and RX).

#pragma once

#include <Arduino.h>

namespace StageLink
{
    // Upper bound on payload bytes for any packet type. Sized with headroom
    // for the largest current payload (a full STATE_SNAPSHOT, see
    // StateSnapshot.h) while staying well under ESP-NOW's 250-byte limit.
    constexpr size_t MAX_PAYLOAD_SIZE = 100;

    // Distinguishes what a packet is for. ReliableRadio treats all of these
    // generically for ack/retry purposes - only the application layer
    // (TX/RX main.cpp) interprets payloads differently per type.
    enum class PacketType : uint8_t
    {
        Heartbeat = 1,       // periodic liveness signal, drives link state
        Cue = 2,              // one-shot show cue trigger
        Dmx = 4,              // reserved for future DMX output
        Configuration = 5,    // reserved for future config packets
        BUTTON_EVENT = 7,     // live encoder-button press/release
        STATE_REQUEST = 8,    // ask peer to resend its full current state
        STATE_SNAPSHOT = 9,   // full state resync, see StateSnapshot.h
        VALUE_UPDATE = 10,    // live single-channel value change (encoder/servo)
        CAPABILITY_REQUEST = 11,  // ask peer to report device info + capabilities
        CAPABILITY_RESPONSE = 12, // reply to CAPABILITY_REQUEST, see DeviceInfo.h
        Acknowledgement = 255 // ack for a received packet's sequence/session
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
        Encoder = 0,
        Servo = 1,
        LedRed = 2,
        LedGreen = 3,
        LedBlue = 4,
        LedBrightness = 5
    };

    constexpr uint8_t VALUE_UPDATE_PAYLOAD_SIZE = 5;

    // VALUE_UPDATE payload is packed manually (not memcpy'd as a struct) so
    // the byte layout is fixed and portable regardless of compiler struct
    // padding/alignment on either board.
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

    // Fixed-size envelope sent as-is over ESP-NOW (memcpy'd whole, see
    // ReliableRadio::sendPacket). packed to guarantee no compiler-inserted
    // padding, since both boards must agree on the exact byte layout.
    //   sequence/sessionId: identify this packet for ack matching and
    //     duplicate detection (a session restarts sequence numbers, e.g.
    //     after a reboot, so both are needed to tell packets apart).
    //   acknowledgedSequence/acknowledgedSessionId: only meaningful on
    //     Acknowledgement packets - echo back what is being acked.
    //   payloadLength/payload: the actual application data; payload is
    //     always MAX_PAYLOAD_SIZE bytes on the wire, only payloadLength of
    //     it is meaningful.
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
            case PacketType::CAPABILITY_REQUEST:
                return "capability request";
            case PacketType::CAPABILITY_RESPONSE:
                return "capability response";
            case PacketType::Acknowledgement:
                return "acknowledgement";
            default:
                return "unknown";
        }
    }
}
