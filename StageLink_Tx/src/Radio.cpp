#include "Radio.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

namespace
{
    uint8_t receiverAddress[] =
    {
        0x68, 0x09, 0x47, 0x3C, 0x49, 0xB4
    };

    bool receiverOnline = false;
    unsigned long lastReceivedTime = 0;
    unsigned long startTime = 0;

    void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
    {
    }

void onDataReceived(
    const uint8_t *mac,
    const uint8_t *data,
    int len)
{
    receiverOnline = true;
    lastReceivedTime = millis();

    Serial.println("RX heartbeat received");
}
}


bool Radio::begin()
{
    WiFi.mode(WIFI_STA);

    Serial.println("Starting ESP-NOW...");

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("ESP-NOW failed");
        return false;
    }
    
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceived);
    esp_now_peer_info_t peerInfo = {};

    memcpy(
        peerInfo.peer_addr,
        receiverAddress,
        6
    );

    peerInfo.channel = 0;
    peerInfo.encrypt = false;


    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add RX");
        return false;
    }

    Serial.println("Radio Ready");

    
    startTime = millis();
    lastReceivedTime = startTime;

    return true;
}


bool Radio::sendMessage(const char* message)
{
    esp_err_t result = esp_now_send(
        receiverAddress,
        (uint8_t*)message,
        strlen(message) + 1
    );

    return result == ESP_OK;
}

bool Radio::isReceiverOnline()
{
    if (millis() - startTime < 3000)
    {
        return true;
    }

    if (millis() - lastReceivedTime > 3000)
    {
        receiverOnline = false;
    }

    return receiverOnline;
}
