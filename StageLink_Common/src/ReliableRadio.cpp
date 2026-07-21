#include "ReliableRadio.h"
#include "RssiMonitor.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_system.h>

namespace
{
    // Only one packet is ever "in flight" at a time (see pendingPacket
    // below) - simpler than a sliding window, and sufficient given
    // StageLink's low message rate. ACK_TIMEOUT_MS/MAX_RETRIES bound how
    // long a single send can take before giving up.
    constexpr unsigned long ACK_TIMEOUT_MS = 300;
    constexpr uint8_t MAX_RETRIES = 3;
    constexpr uint8_t QUEUE_SIZE = 8;
    // How long without hearing from the peer before isPeerOnline() flips false.
    constexpr unsigned long ONLINE_TIMEOUT_MS = 3000;
    // Grace period after begin() during which the peer is assumed online,
    // so the link doesn't immediately report "lost" before a first packet
    // has had a chance to arrive.
    constexpr unsigned long STARTUP_GRACE_MS = 3000;

    // A packet as received off the air, tagged with the sender's MAC so
    // RX (which doesn't know TX's address up front) can learn its peer.
    struct IncomingPacket
    {
        StageLink::Packet packet;
        uint8_t mac[6];
    };

    // The single packet currently awaiting an Acknowledgement.
    struct PendingPacket
    {
        bool active = false;
        StageLink::Packet packet = {};
        uint8_t attempts = 0;
        unsigned long firstSentTime = 0; // first send time, for round-trip measurement
        unsigned long lastSentTime = 0;  // most recent (re)send time, for ACK_TIMEOUT_MS
    };

    StageLink::ReliableRadio *radioInstance = nullptr;
    // Guards receiveQueue since onDataReceived runs on the WiFi task, not
    // the Arduino loop() task - everything else here is single-threaded.
    portMUX_TYPE receiveQueueLock = portMUX_INITIALIZER_UNLOCKED;

    // Raw packets off the air, waiting for update() to process them.
    IncomingPacket receiveQueue[QUEUE_SIZE] = {};
    uint8_t receiveHead = 0;
    uint8_t receiveTail = 0;

    // Outgoing packets queued by send(), waiting to become the pendingPacket.
    StageLink::Packet sendQueue[QUEUE_SIZE] = {};
    uint8_t sendHead = 0;
    uint8_t sendTail = 0;

    // Received packets already acked and de-duplicated, waiting for the
    // application to consume via ReliableRadio::receive().
    StageLink::Packet applicationQueue[QUEUE_SIZE] = {};
    uint8_t applicationHead = 0;
    uint8_t applicationTail = 0;

    // Delivery outcomes waiting for the application to consume via
    // ReliableRadio::getSendResult().
    StageLink::SendResult resultQueue[QUEUE_SIZE] = {};
    uint8_t resultHead = 0;
    uint8_t resultTail = 0;

    PendingPacket pendingPacket;
    uint8_t defaultPeer[6] = {};
    bool hasDefaultPeer = false;
    // Sequence numbers start at 1 (0 is never assigned) purely so an
    // all-zero/uninitialized packet can't be mistaken for a real one.
    uint16_t nextSequence = 1;
    // Randomized per boot so a rebooted board's fresh sequence numbers
    // can't collide with duplicate-detection state the peer kept from
    // before the reboot.
    uint16_t localSessionId = 0;
    // Sequence/session of the last packet delivered to the application -
    // used to drop retransmitted duplicates before they reach it.
    uint16_t lastDeliveredSequence = 0;
    uint16_t lastDeliveredSessionId = 0;
    bool hasLastDeliveredSequence = false;
    unsigned long startTime = 0;
    unsigned long lastReceivedTime = 0;
    // Round-trip / retry stats, surfaced via diagnostics() for the status displays.
    unsigned long lastRoundTripMs = 0;
    unsigned long roundTripSumMs = 0;
    unsigned long maxRoundTripMs = 0;
    uint32_t roundTripSamples = 0;
    uint8_t lastRetryCount = 0;
    uint32_t packetsSent = 0;
    uint32_t packetsAcknowledged = 0;
    uint32_t packetsFailed = 0;
    uint32_t packetsReceived = 0;
    uint32_t totalRetries = 0;
    uint32_t duplicatePackets = 0;

    uint8_t nextIndex(uint8_t index)
    {
        return (index + 1) % QUEUE_SIZE;
    }

    // ESP-NOW requires a peer to be registered before sending to it.
    bool ensurePeer(const uint8_t *mac)
    {
        if (esp_now_is_peer_exist(mac))
        {
            return true;
        }

        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, mac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;

        return esp_now_add_peer(&peerInfo) == ESP_OK;
    }

    // Actually puts a packet on the air. The whole StagePacket struct is
    // sent as raw bytes - both boards must agree on its exact layout.
    bool sendPacket(const StageLink::Packet &packet, const uint8_t *mac)
    {
        if (!ensurePeer(mac))
        {
            return false;
        }

        return esp_now_send(
            mac,
            reinterpret_cast<const uint8_t *>(&packet),
            sizeof(packet)
        ) == ESP_OK;
    }

    void addResult(
        StageLink::PacketType type,
        uint16_t sequence,
        StageLink::SendStatus status
    )
    {
        uint8_t next = nextIndex(resultHead);
        if (next == resultTail)
        {
            return;
        }

        resultQueue[resultHead] = { type, sequence, status };
        resultHead = next;
    }

    // Sends (or re-sends) the current pendingPacket. Called both for the
    // first attempt and for every retry - attempts > 0 marks a retry for
    // the totalRetries counter.
    void transmitPendingPacket()
    {
        if (!hasDefaultPeer)
        {
            return;
        }

        if (pendingPacket.attempts > 0)
        {
            totalRetries++;
        }

        sendPacket(pendingPacket.packet, defaultPeer);
        pendingPacket.attempts++;
        pendingPacket.lastSentTime = millis();
    }
}

bool StageLink::ReliableRadio::begin(const uint8_t *initialPeer)
{
    radioInstance = this;

    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK)
    {
        return false;
    }

    esp_now_register_recv_cb(onDataReceived);

    RssiMonitor::begin();

    // TX knows RX's address ahead of time; RX does not know TX's, so it
    // waits and adopts whichever peer it first hears from (see update()).
    if (initialPeer != nullptr)
    {
        memcpy(defaultPeer, initialPeer, 6);
        hasDefaultPeer = true;
        RssiMonitor::setPeer(defaultPeer);

        if (!ensurePeer(defaultPeer))
        {
            return false;
        }
    }

    startTime = millis();
    lastReceivedTime = startTime;
    // esp_random() so a rebooted board doesn't reuse the same session ID
    // (and thus sequence numbers) as its previous boot; 0 is reserved as
    // "no session" so it's remapped to 1 on the rare chance it comes up.
    localSessionId = static_cast<uint16_t>(esp_random());

    if (localSessionId == 0)
    {
        localSessionId = 1;
    }

    return true;
}

bool StageLink::ReliableRadio::send(
    PacketType type,
    const uint8_t *payload,
    uint8_t payloadLength
)
{
    // Acknowledgement is an internal-only packet type generated by
    // update() itself; application code is not allowed to send one directly.
    if (type == PacketType::Acknowledgement ||
        payloadLength > MAX_PAYLOAD_SIZE)
    {
        return false;
    }

    uint8_t next = nextIndex(sendHead);
    if (next == sendTail)
    {
        return false;
    }

    Packet packet = {};
    packet.type = type;
    packet.sequence = nextSequence++;
    packet.sessionId = localSessionId;
    packet.payloadLength = payloadLength;
    memcpy(packet.payload, payload, packet.payloadLength);

    sendQueue[sendHead] = packet;
    sendHead = next;

    return true;
}

bool StageLink::ReliableRadio::send(PacketType type, const char *payload)
{
    return send(
        type,
        reinterpret_cast<const uint8_t *>(payload),
        strlen(payload)
    );
}

void StageLink::ReliableRadio::update()
{
    // Pull one raw packet (if any) out of the ISR-fed receive queue.
    IncomingPacket incoming = {};
    bool hasIncoming = false;

    portENTER_CRITICAL(&receiveQueueLock);
    if (receiveTail != receiveHead)
    {
        incoming = receiveQueue[receiveTail];
        receiveTail = nextIndex(receiveTail);
        hasIncoming = true;
    }
    portEXIT_CRITICAL(&receiveQueueLock);

    if (hasIncoming)
    {
        // Adopt whoever we just heard from as the peer to send to - this is
        // how RX learns TX's MAC without it being configured up front.
        memcpy(defaultPeer, incoming.mac, 6);
        hasDefaultPeer = true;
        RssiMonitor::setPeer(defaultPeer);
        lastReceivedTime = millis();

        if (incoming.packet.type == PacketType::Acknowledgement)
        {
            // Only matters if it acks the packet we're currently waiting
            // on - stray/late acks for anything else are silently dropped.
            if (pendingPacket.active &&
                incoming.packet.acknowledgedSequence == pendingPacket.packet.sequence &&
                incoming.packet.acknowledgedSessionId == pendingPacket.packet.sessionId)
            {
                addResult(
                    pendingPacket.packet.type,
                    pendingPacket.packet.sequence,
                    SendStatus::Acknowledged
                );
                lastRoundTripMs = millis() - pendingPacket.firstSentTime;
                roundTripSumMs += lastRoundTripMs;
                roundTripSamples++;
                if (lastRoundTripMs > maxRoundTripMs)
                {
                    maxRoundTripMs = lastRoundTripMs;
                }
                lastRetryCount = pendingPacket.attempts - 1;
                packetsAcknowledged++;
                pendingPacket.active = false;
            }
        }
        else
        {
            // Every non-ack packet gets acked immediately, whether or not
            // it turns out to be a duplicate - the sender only cares that
            // it arrived, not whether we'd already seen it before.
            StageLink::Packet acknowledgement = {};
            acknowledgement.type = StageLink::PacketType::Acknowledgement;
            acknowledgement.sequence = nextSequence++;
            acknowledgement.sessionId = localSessionId;
            acknowledgement.acknowledgedSequence = incoming.packet.sequence;
            acknowledgement.acknowledgedSessionId = incoming.packet.sessionId;
            sendPacket(acknowledgement, incoming.mac);

            // Duplicate protection: if our ack to the sender was lost, it
            // will retry the same sequence/session - only compared against
            // the single last-delivered packet, since only one send is
            // ever in flight per sender at a time.
            bool isDuplicate = hasLastDeliveredSequence &&
                               incoming.packet.sessionId == lastDeliveredSessionId &&
                               incoming.packet.sequence == lastDeliveredSequence;

            if (!isDuplicate)
            {
                uint8_t next = nextIndex(applicationHead);
                if (next != applicationTail)
                {
                    applicationQueue[applicationHead] = incoming.packet;
                    applicationHead = next;
                    lastDeliveredSequence = incoming.packet.sequence;
                    lastDeliveredSessionId = incoming.packet.sessionId;
                    hasLastDeliveredSequence = true;
                    packetsReceived++;
                }
            }
            else
            {
                duplicatePackets++;
            }
        }
    }

    // Start the next queued send only once the previous one has finished
    // (acked or given up) - this is what makes "one packet in flight" hold.
    if (!pendingPacket.active && sendTail != sendHead)
    {
        pendingPacket.packet = sendQueue[sendTail];
        sendTail = nextIndex(sendTail);
        pendingPacket.active = true;
        pendingPacket.attempts = 0;
        pendingPacket.firstSentTime = millis();
        packetsSent++;
        transmitPendingPacket();
    }

    // No ack within ACK_TIMEOUT_MS: retry, or give up as Failed after
    // MAX_RETRIES so the application isn't left waiting forever.
    if (pendingPacket.active &&
        millis() - pendingPacket.lastSentTime >= ACK_TIMEOUT_MS)
    {
        if (pendingPacket.attempts <= MAX_RETRIES)
        {
            transmitPendingPacket();
        }
        else
        {
            addResult(
                pendingPacket.packet.type,
                pendingPacket.packet.sequence,
                SendStatus::Failed
            );
            lastRetryCount = pendingPacket.attempts - 1;
            packetsFailed++;
            pendingPacket.active = false;
        }
    }
}

bool StageLink::ReliableRadio::receive(StagePacket &packet)
{
    if (applicationTail == applicationHead)
    {
        return false;
    }

    packet = applicationQueue[applicationTail];
    applicationTail = nextIndex(applicationTail);
    return true;
}

bool StageLink::ReliableRadio::getSendResult(SendResult &result)
{
    if (resultTail == resultHead)
    {
        return false;
    }

    result = resultQueue[resultTail];
    resultTail = nextIndex(resultTail);
    return true;
}

bool StageLink::ReliableRadio::isPeerOnline() const
{
    // Assume online right after boot so the link doesn't flash "lost"
    // before the peer has even had a chance to say hello.
    if (millis() - startTime < STARTUP_GRACE_MS)
    {
        return true;
    }

    return millis() - lastReceivedTime <= ONLINE_TIMEOUT_MS;
}

StageLink::RadioDiagnostics StageLink::ReliableRadio::diagnostics() const
{
    int8_t rssi = 0;
    bool rssiAvailable = RssiMonitor::getRssi(rssi);

    unsigned long averageRoundTripMs =
        roundTripSamples > 0 ? roundTripSumMs / roundTripSamples : 0;

    return {
        isPeerOnline(),
        rssiAvailable,
        rssi,
        lastRoundTripMs,
        averageRoundTripMs,
        maxRoundTripMs,
        lastRetryCount,
        packetsSent,
        packetsAcknowledged,
        packetsFailed,
        packetsReceived,
        totalRetries,
        duplicatePackets
    };
}

// Registered with esp_now_register_recv_cb; runs in the WiFi driver's task
// context, so it does the minimum possible (bounds check + copy into a
// lock-protected queue) and leaves all real handling to update().
void StageLink::ReliableRadio::onDataReceived(
    const uint8_t *mac,
    const uint8_t *data,
    int length
)
{
    // A length mismatch means this wasn't a StagePacket from another
    // StageLink board (or the struct layout changed on one side only) -
    // drop it rather than risk misinterpreting the bytes.
    if (radioInstance == nullptr || length != sizeof(Packet))
    {
        return;
    }

    portENTER_CRITICAL(&receiveQueueLock);

    uint8_t next = nextIndex(receiveHead);
    if (next != receiveTail)
    {
        memcpy(receiveQueue[receiveHead].mac, mac, 6);
        memcpy(&receiveQueue[receiveHead].packet, data, sizeof(Packet));
        receiveHead = next;
    }

    portEXIT_CRITICAL(&receiveQueueLock);
}
