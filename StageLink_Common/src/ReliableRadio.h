// StageLink ReliableRadio
// Handles all ESP-NOW communication between TX and RX.
// Provides ACKs, retries, sequence tracking, and duplicate packet
// protection on top of ESP-NOW's unreliable, unordered delivery.
// Application code (TX/RX main.cpp) should use this layer only - it
// should never touch esp_now_* APIs directly.
// Belongs to: StageLink_Common (shared by TX and RX).
//
// Packet flow:
//   send()      queues one outgoing packet (non-blocking).
//   update()    must be polled every loop(); it drives retransmission of
//               the one in-flight packet, replies to received packets
//               with an Acknowledgement, and files completed sends into
//               the result queue.
//   receive()   drains application-layer packets already delivered by
//               update() (acks and duplicates are filtered out first).
//   getSendResult() drains Acknowledged/Failed outcomes for packets this
//               board sent, so callers can react to delivery failures.

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

    // Outcome of one previously-queued send(), reported once via
    // getSendResult() so callers know whether a packet actually arrived.
    struct SendResult
    {
        PacketType type;
        uint16_t sequence;
        SendStatus status;
    };

    // Snapshot of link health for display/debugging (see Display::showDiagnostics).
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
        // initialPeer: TX passes its known RX MAC address so it can send
        // before hearing anything back. RX omits it and instead learns its
        // peer's MAC from the first packet it receives (see update()).
        bool begin(const uint8_t *initialPeer = nullptr);

        // Queues one outgoing packet; returns false if the send queue is
        // full or payloadLength exceeds MAX_PAYLOAD_SIZE. Non-blocking -
        // actual transmission and retries happen in update().
        bool send(
            PacketType type,
            const uint8_t *payload,
            uint8_t payloadLength
        );

        bool send(PacketType type, const char *payload = "");

        // Drives the whole reliable-delivery state machine: processes one
        // received packet (ack matching, or ack-and-enqueue for others),
        // starts the next queued send if idle, and retransmits/times out
        // the current in-flight send. Must be called every loop().
        void update();

        // Pops one already-delivered, de-duplicated application packet.
        // Acks and duplicate retransmissions never appear here.
        bool receive(StagePacket &packet);

        // Pops one outcome (Acknowledged/Failed) for a packet this board
        // previously sent via send().
        bool getSendResult(SendResult &result);

        // True during a startup grace period (peer assumed present until
        // proven otherwise) or if a packet was heard from the peer within
        // ONLINE_TIMEOUT_MS.
        bool isPeerOnline() const;

        RadioDiagnostics diagnostics() const;

    private:
        // ESP-NOW receive callback (runs in the WiFi task context, not
        // loop()). Only copies bytes into a lock-protected queue for
        // update() to process - keeps ISR-context work minimal.
        static void onDataReceived(
            const uint8_t *mac,
            const uint8_t *data,
            int length
        );
    };
}
