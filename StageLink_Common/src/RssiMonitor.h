#pragma once

#include <Arduino.h>

namespace StageLink
{
    // Listens for 802.11 action frames (what ESP-NOW sends) in promiscuous
    // mode to recover RSSI, since esp_now_recv_cb on this SDK (ESP-IDF 4.4)
    // does not expose it. Read-only: never transmits, so it does not create
    // a second communication path.
    class RssiMonitor
    {
    public:
        static bool begin();

        static void setPeer(const uint8_t *mac);

        static bool getRssi(int8_t &rssi);
    };
}
