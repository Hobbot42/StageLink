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

    struct RadioDiagnostics
    {
        bool peerOnline;
        bool rssiAvailable;
        int8_t rssi;
        unsigned long lastRoundTripMs;
        unsigned long averageRoundTripMs;
        unsigned long maxRoundTripMs;
        uint8_t lastRetryCount;
        uint32_t packetsSent;
        uint32_t packetsAcknowledged;
        uint32_t packetsFailed;
        uint32_t packetsReceived;
        uint32_t totalRetries;
        uint32_t duplicatePackets;
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

        RadioDiagnostics diagnostics() const;

    private:
        static void onDataReceived(
            const uint8_t *mac,
            const uint8_t *data,
            int length
        );
    };
}
