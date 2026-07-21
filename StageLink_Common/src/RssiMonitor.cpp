#include "RssiMonitor.h"

#include <esp_wifi.h>
#include <cstring>

namespace
{
    // Minimal 802.11 MAC header, just enough to read frame type/subtype
    // and the sender address (addr2) out of a promiscuous-mode capture.
    struct WifiIeee80211MacHeader
    {
        uint16_t frameControl;
        uint16_t duration;
        uint8_t addr1[6];
        uint8_t addr2[6];
        uint8_t addr3[6];
        uint16_t sequenceControl;
    } __attribute__((packed));

    constexpr uint8_t FRAME_TYPE_MANAGEMENT = 0x00;
    constexpr uint8_t FRAME_SUBTYPE_ACTION = 0x0D;

    uint8_t peerMac[6] = {};
    bool hasPeer = false;
    volatile int8_t lastRssi = 0;
    volatile bool hasRssi = false;

    // Runs for every management frame overheard on this WiFi channel.
    // ESP-NOW frames are 802.11 action frames, so filtering down to
    // management/action + the known peer's address isolates just our
    // radio traffic without needing to parse ESP-NOW's own payload.
    void IRAM_ATTR handlePromiscuousRx(void *buffer, wifi_promiscuous_pkt_type_t type)
    {
        if (type != WIFI_PKT_MGMT || !hasPeer)
        {
            return;
        }

        const wifi_promiscuous_pkt_t *packet =
            reinterpret_cast<const wifi_promiscuous_pkt_t *>(buffer);

        if (packet->rx_ctrl.sig_len < sizeof(WifiIeee80211MacHeader))
        {
            return;
        }

        const WifiIeee80211MacHeader *header =
            reinterpret_cast<const WifiIeee80211MacHeader *>(packet->payload);

        uint8_t frameType = (header->frameControl >> 2) & 0x03;
        uint8_t frameSubtype = (header->frameControl >> 4) & 0x0F;

        if (frameType != FRAME_TYPE_MANAGEMENT || frameSubtype != FRAME_SUBTYPE_ACTION)
        {
            return;
        }

        if (memcmp(header->addr2, peerMac, 6) != 0)
        {
            return;
        }

        lastRssi = packet->rx_ctrl.rssi;
        hasRssi = true;
    }
}

bool StageLink::RssiMonitor::begin()
{
    if (esp_wifi_set_promiscuous(true) != ESP_OK)
    {
        return false;
    }

    wifi_promiscuous_filter_t filter = {};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;

    if (esp_wifi_set_promiscuous_filter(&filter) != ESP_OK)
    {
        return false;
    }

    return esp_wifi_set_promiscuous_rx_cb(handlePromiscuousRx) == ESP_OK;
}

void StageLink::RssiMonitor::setPeer(const uint8_t *mac)
{
    if (hasPeer && memcmp(peerMac, mac, 6) == 0)
    {
        return;
    }

    memcpy(peerMac, mac, 6);
    hasPeer = true;
    // Old RSSI belonged to a different peer (or the same peer's stale
    // reading) - clear it so getRssi() doesn't report a value that
    // doesn't correspond to the current link.
    hasRssi = false;
}

bool StageLink::RssiMonitor::getRssi(int8_t &rssi)
{
    if (!hasRssi)
    {
        return false;
    }

    rssi = lastRssi;
    return true;
}
