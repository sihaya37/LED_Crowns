#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <FastLED.h>
#include "config.h"

// --- Variables Globales ---
CRGB leds[NUM_LEDS];
struct_message incomingData;
uint32_t lastMsgId = 0;
uint8_t currentEffect = 1;

// --- Prototypes des Effets ---
void effectTestRouge();
void effectTestBleu();
void effectTestVert();

// --- Callback de Réception & Relais ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingRawData, int len) {
    memcpy(&incomingData, incomingRawData, sizeof(incomingData));

    // 1. Protection contre les doublons
    if (incomingData.msgId <= lastMsgId) return;
    lastMsgId = incomingData.msgId;

    // 2. Mise à jour de l'effet local
    currentEffect = incomingData.effectMode;
    Serial.printf("ID: %u | Effet: %d | Saut: %d\n", incomingData.msgId, currentEffect, incomingData.hopCount);

    // 3. Logique de Relais (Perroquet)
    if (incomingData.hopCount < 2) { 
        incomingData.hopCount++;
        for(int i = 0; i < MSG_REDUNDANCY_COUNT; i++) {
            esp_now_send(BROADCAST_ADDR, (uint8_t *) &incomingData, sizeof(incomingData));
            delay(MSG_REDUNDANCY_DELAY);
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialisation FastLED (Pin 3 pour tes ESP32-C3)
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50); // Luminosité réduite pour les tests

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

    // Enregistrement du peer pour permettre le relais
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDR, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.println("Node prêt et en attente de messages...");
}

void loop() {
    // Application de l'effet selon le mode reçu
    switch (currentEffect) {
        case 1: effectTestRouge(); break;
        case 2: effectTestBleu();  break;
        case 3: effectTestVert();  break;
        default: FastLED.clear();  break;
    }
    FastLED.show();
}

// --- Fonctions de Test ---
void effectTestRouge() {
    fill_solid(leds, NUM_LEDS, CRGB::Red);
}

void effectTestBleu() {
    fill_solid(leds, NUM_LEDS, CRGB::Blue);
}

void effectTestVert() {
    fill_solid(leds, NUM_LEDS, CRGB::Green);
}