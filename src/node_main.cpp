#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>
#include "config.h"

CRGB leds[NUM_LEDS];

uint32_t lastProcessedId = 0;
unsigned long lastSignalTime = 0;

void showColor(const CRGB& color) {
    fill_solid(leds, NUM_LEDS, color);
    FastLED.show();
}

void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(struct_message)) {
        return;
    }

    struct_message incoming;
    memcpy(&incoming, data, sizeof(incoming));

    // Filtre anti-doublon
    if (incoming.msgId <= lastProcessedId) {
        return;
    }

    lastProcessedId = incoming.msgId;
    lastSignalTime = millis();

    // Affichage couleur fixe selon le nombre de rebonds déjà effectués
    if (incoming.hopCount == 0) {
        showColor(CRGB::Blue);      // direct master
    } else if (incoming.hopCount == 1) {
        showColor(CRGB::Green);     // via 1 intermédiaire
    } else if (incoming.hopCount == 2) {
        showColor(CRGB::Orange);    // via 2 intermédiaires
    } else {
        showColor(CRGB::Purple);    // via 3 intermédiaires ou plus
    }

    // Relais si on n'a pas atteint la limite
    if (incoming.hopCount < MAX_HOPS) {
        incoming.hopCount++;

        WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4));

        for (int i = 0; i < MSG_REDUNDANCY_COUNT; i++) {
            esp_now_send(BROADCAST_ADDR, (uint8_t*)&incoming, sizeof(incoming));
            delay(MSG_REDUNDANCY_DELAY);
        }
    }
}

void setup() {
    Serial.begin(115200);

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setMaxPowerInVoltsAndMilliamps(LED_POWER_VOLTAGE, LED_POWER_MILLIAMPS);
    showColor(CRGB(15, 0, 0)); // rouge au démarrage tant qu'aucun signal n'est reçu

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

    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

    WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4));
    Serial.printf("Node prêt. Canal=%d, TX=%d\n", WIFI_CHANNEL, TX_POWER_LEVEL);
}

void loop() {
    if (millis() - lastSignalTime > SIGNAL_TIMEOUT) {
        showColor(CRGB(15, 0, 0)); // rouge fixe sans signal
    }

    delay(20);
}