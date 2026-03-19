#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"

uint32_t msgCounter = 0;

void setup() {
    Serial.begin(115200);
    
    // Configuration WiFi pour ESP-NOW
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur Initialisation ESP-NOW");
        return;
    }

    // Enregistrement du Broadcast
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDR, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.println("MASTER WROOM Prêt - Canal " + String(WIFI_CHANNEL));
}

void loop() {
    static unsigned long lastSend = 0;
    
    // Envoi automatique toutes les 2 secondes (pour test)
    if (millis() - lastSend > 2000) {
        lastSend = millis();
        msgCounter++;

        struct_message data;
        data.msgId = msgCounter;
        data.hopCount = 0;
        data.txPower = TX_POWER_LEVEL;

        // Réglage puissance et envoi redondant
        WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4)); 
        for(int i = 0; i < MSG_REDUNDANCY_COUNT; i++) {
            esp_now_send(BROADCAST_ADDR, (uint8_t *) &data, sizeof(data));
            delay(MSG_REDUNDANCY_DELAY);
        }
        Serial.printf("Message %u envoyé (Redondance x%d)\n", msgCounter, MSG_REDUNDANCY_COUNT);
    }
}