#pragma once

#include <Arduino.h>
#include "StageLinkProtocol.h"

namespace StageLink
{
    enum class SendStatus : uint8_t
    {
        Acknowledged,
        Failed
    };

    struct SendResult
    {
        PacketType type;
        uint16_t sequence;
        SendStatus status;
    };

    class ReliableRadio
    {
    public:
        bool begin(const uint8_t *initialPeer = nullptr);

        bool send(
            PacketType type,
            const uint8_t *payload,
            uint8_t payloadLength
        );

        bool send(PacketType type, const char *payload = "");

        void update();

        bool receive(StagePacket &packet);

        bool getSendResult(SendResult &result);

        bool isPeerOnline() const;

    private:
        static void onDataReceived(
            const uint8_t *mac,
            const uint8_t *data,
            int length
        );
    };
}
