#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <MFRC522.h>
#include "config.h"

#define RST_PIN 22
#define SS_PIN  21

MFRC522 rfid(SS_PIN, RST_PIN);

struct_message myData;
uint32_t msgCounter = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- MASTER START ---");

    SPI.begin();
    rfid.PCD_Init();

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur ESP-NOW init");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDR, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Erreur ajout peer broadcast");
        return;
    }

    WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4));
    Serial.printf("Master prêt. Canal=%d, TX=%d\n", WIFI_CHANNEL, TX_POWER_LEVEL);
}

void loop() {
    static unsigned long lastSend = 0;

    if (millis() - lastSend > 2000) {
        lastSend = millis();
        msgCounter++;

        myData.msgId = msgCounter;
        myData.hopCount = 0;

        for (int i = 0; i < MSG_REDUNDANCY_COUNT; i++) {
            esp_now_send(BROADCAST_ADDR, (uint8_t*)&myData, sizeof(myData));
            delay(MSG_REDUNDANCY_DELAY);
        }

        Serial.printf("Envoyé ID: %u\n", msgCounter);
    }
}