#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "StatusLED.h"

unsigned long lastMessageTime = 0;

void sendReply(const uint8_t *mac)
{
    const char reply[] = "RX ALIVE";

    if (!esp_now_is_peer_exist(mac))
    {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, mac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;

        if (esp_now_add_peer(&peerInfo) != ESP_OK)
        {
            Serial.println("Failed to add TX peer");
            return;
        }
    }

    esp_err_t result = esp_now_send(
        mac,
        (uint8_t *)reply,
        sizeof(reply)
    );

    if (result != ESP_OK)
    {
        Serial.println("RX ALIVE send failed");
        return;
    }

    Serial.println("Sent: RX ALIVE");
    StatusLED::setReady();
}


void onDataReceive(
    const uint8_t *mac,
    const uint8_t *data,
    int length
)
{
    lastMessageTime = millis();

    Serial.print("Received: ");

    for(int i = 0; i < length; i++)
    {
        Serial.print((char)data[i]);
    }

    Serial.println();

    StatusLED::setReady();

    sendReply(mac);
}

void setup()
{
    Serial.begin(115200);

    StatusLED::begin();
    StatusLED::setOffline();

    WiFi.mode(WIFI_STA);

    Serial.println();
    Serial.println("StageLink RX");
    Serial.println("Starting ESP-NOW...");

if (esp_now_init() != ESP_OK)
{
    Serial.println("ESP-NOW failed");
    return;
}

esp_now_register_recv_cb(onDataReceive);

    Serial.println("RX Ready");
}


void loop()
{
    if (millis() - lastMessageTime > 3000)
    {
        StatusLED::setOffline();
    }

    // existing code stays here
}
