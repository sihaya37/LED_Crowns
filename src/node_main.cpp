#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>
#include "config.h"

CRGB leds[NUM_LEDS];
uint32_t lastProcessedId = 0;
unsigned long lastSignalTime = 0;

// Callback de réception
void OnDataRecv(const uint8_t * mac, const uint8_t *data, int len) {
    struct_message incoming;
    memcpy(&incoming, data, sizeof(incoming));

    if (incoming.msgId <= lastProcessedId) return;
    lastProcessedId = incoming.msgId;
    lastSignalTime = millis();

    // Déclenchement de la couleur (sera affiché par la loop)
    if (incoming.hopCount == 0)      fill_solid(leds, NUM_LEDS, CRGB::Blue);
    else if (incoming.hopCount == 1) fill_solid(leds, NUM_LEDS, CRGB::Green);
    else                             fill_solid(leds, NUM_LEDS, CRGB::Orange);

    // RELAIS IMMÉDIAT (Avant tout traitement lourd)
    if (incoming.hopCount < 2) { 
        incoming.hopCount++;
        // On remet exactement ta logique de puissance validée
        WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4)); 
        for(int i = 0; i < MSG_REDUNDANCY_COUNT; i++) {
            esp_now_send(BROADCAST_ADDR, (uint8_t *) &incoming, sizeof(incoming));
            // Note : delayMicroseconds est souvent préférable à delay() dans une callback
            delayMicroseconds(MSG_REDUNDANCY_DELAY * 1000); 
        }
    }
}

void setup() {
    Serial.begin(115200);

    // --- SÉCURITÉ ÉLECTRIQUE ---
    // Limite à 5V et 450mA (préserve ta batterie 1000mAh et tes pistes)
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 450); 
    // FastLED.setBrightness(100); // On peut monter le réglage, le limiteur protègera le hardware

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    }

    // Peer nécessaire pour renvoyer le signal (relais)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDR, 6);
    peerInfo.channel = WIFI_CHANNEL;
    esp_now_add_peer(&peerInfo);
}

void loop() {
    // Gestion de la veille (Rouge très faible après 2s de silence)
    if (millis() - lastSignalTime > 2000) {
        fill_solid(leds, NUM_LEDS, CRGB(10, 0, 0));
    } else {
        // Extinction progressive après le flash
        fadeToBlackBy(leds, NUM_LEDS, 20);
    }
    FastLED.show();
    delay(30);
}