#include "ReliableRadio.h"

#include <WiFi.h>
#include <esp_now.h>

namespace
{
    constexpr unsigned long ACK_TIMEOUT_MS = 300;
    constexpr uint8_t MAX_RETRIES = 3;
    constexpr uint8_t QUEUE_SIZE = 8;
    constexpr unsigned long ONLINE_TIMEOUT_MS = 3000;
    constexpr unsigned long STARTUP_GRACE_MS = 3000;

    struct IncomingPacket
    {
        StageLink::Packet packet;
        uint8_t mac[6];
    };

    struct PendingPacket
    {
        bool active = false;
        StageLink::Packet packet = {};
        uint8_t attempts = 0;
        unsigned long lastSentTime = 0;
    };

    StageLink::ReliableRadio *radioInstance = nullptr;
    portMUX_TYPE receiveQueueLock = portMUX_INITIALIZER_UNLOCKED;

    IncomingPacket receiveQueue[QUEUE_SIZE] = {};
    uint8_t receiveHead = 0;
    uint8_t receiveTail = 0;

    StageLink::Packet sendQueue[QUEUE_SIZE] = {};
    uint8_t sendHead = 0;
    uint8_t sendTail = 0;

    StageLink::Packet applicationQueue[QUEUE_SIZE] = {};
    uint8_t applicationHead = 0;
    uint8_t applicationTail = 0;

    StageLink::SendResult resultQueue[QUEUE_SIZE] = {};
    uint8_t resultHead = 0;
    uint8_t resultTail = 0;

    PendingPacket pendingPacket;
    uint8_t defaultPeer[6] = {};
    bool hasDefaultPeer = false;
    uint16_t nextSequence = 1;
    uint16_t lastDeliveredSequence = 0;
    bool hasLastDeliveredSequence = false;
    unsigned long startTime = 0;
    unsigned long lastReceivedTime = 0;

    uint8_t nextIndex(uint8_t index)
    {
        return (index + 1) % QUEUE_SIZE;
    }

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

    void sendAcknowledgement(const uint8_t *mac, uint16_t acknowledgedSequence)
    {
        StageLink::Packet acknowledgement = {};
        acknowledgement.type = StageLink::PacketType::Acknowledgement;
        acknowledgement.sequence = nextSequence++;
        acknowledgement.acknowledgedSequence = acknowledgedSequence;

        // ACK packets are control responses. Acknowledging an ACK would create a loop.
        sendPacket(acknowledgement, mac);
    }

    void transmitPendingPacket()
    {
        if (!hasDefaultPeer)
        {
            return;
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

    if (initialPeer != nullptr)
    {
        memcpy(defaultPeer, initialPeer, 6);
        hasDefaultPeer = true;

        if (!ensurePeer(defaultPeer))
        {
            return false;
        }
    }

    startTime = millis();
    lastReceivedTime = startTime;

    return true;
}

bool StageLink::ReliableRadio::send(PacketType type, const char *payload)
{
    if (type == PacketType::Acknowledgement)
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
    packet.payloadLength = min(strlen(payload), MAX_PAYLOAD_SIZE);
    memcpy(packet.payload, payload, packet.payloadLength);

    sendQueue[sendHead] = packet;
    sendHead = next;

    return true;
}

void StageLink::ReliableRadio::update()
{
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
        memcpy(defaultPeer, incoming.mac, 6);
        hasDefaultPeer = true;
        lastReceivedTime = millis();

        if (incoming.packet.type == PacketType::Acknowledgement)
        {
            if (pendingPacket.active &&
                incoming.packet.acknowledgedSequence == pendingPacket.packet.sequence)
            {
                addResult(
                    pendingPacket.packet.type,
                    pendingPacket.packet.sequence,
                    SendStatus::Acknowledged
                );
                pendingPacket.active = false;
            }
        }
        else
        {
            sendAcknowledgement(incoming.mac, incoming.packet.sequence);

            bool isDuplicate = hasLastDeliveredSequence &&
                               incoming.packet.sequence == lastDeliveredSequence;

            if (!isDuplicate)
            {
                uint8_t next = nextIndex(applicationHead);
                if (next != applicationTail)
                {
                    applicationQueue[applicationHead] = incoming.packet;
                    applicationHead = next;
                    lastDeliveredSequence = incoming.packet.sequence;
                    hasLastDeliveredSequence = true;
                }
            }
        }
    }

    if (!pendingPacket.active && sendTail != sendHead)
    {
        pendingPacket.packet = sendQueue[sendTail];
        sendTail = nextIndex(sendTail);
        pendingPacket.active = true;
        pendingPacket.attempts = 0;
        transmitPendingPacket();
    }

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
            pendingPacket.active = false;
        }
    }
}

bool StageLink::ReliableRadio::receive(Packet &packet)
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
    if (millis() - startTime < STARTUP_GRACE_MS)
    {
        return true;
    }

    return millis() - lastReceivedTime <= ONLINE_TIMEOUT_MS;
}

void StageLink::ReliableRadio::onDataReceived(
    const uint8_t *mac,
    const uint8_t *data,
    int length
)
{
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
