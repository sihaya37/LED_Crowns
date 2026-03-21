#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
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
void effectScintillement();

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
    
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // Réglage de la puissance d'émission - NE PAS SUPPRIMER
    WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4)); 

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
    // 1. Fond légèrement scintillant
    for (int i = 0; i < NUM_LEDS; i++) {
        // On réduit lentement la luminosité de chaque LED (effet traînée)        
        // Plus la valeur est petite (ex: 15), plus les leds s'éteignent lentement
        leds[i].fadeToBlackBy(1); 
    }

    // 2. Apparition de "scintilles" aléatoires
    if (random8() < 15) { // Plus ce nombre est élevé plus il y a de leds qui s'allument
        int pos = random16(NUM_LEDS);        
        
        // On choisit une luminosité aléatoire entre 100 et 255
        uint8_t randomBrightness = random8(0, 170);
        
        // CHSV(Teinte, Saturation, Valeur)
        // Saturation 0 = Blanc pur
        leds[pos] = CHSV(0, 0, randomBrightness);
    }    
}

void effectTestBleu() {
    fill_solid(leds, NUM_LEDS, CRGB::Blue);
}

void effectTestVert() {
    fill_solid(leds, NUM_LEDS, CRGB::Green);
}

void effectScintillement() {
    // 1. Fond légèrement scintillant
    for (int i = 0; i < NUM_LEDS; i++) {
        // On réduit lentement la luminosité de chaque LED (effet traînée)        
        // Plus la valeur est petite (ex: 15), plus les leds s'éteignent lentement
        leds[i].fadeToBlackBy(1); 
    }

    // 2. Apparition de "scintilles" aléatoires
    if (random8() < 15) { // Plus ce nombre est élevé plus il y a de leds qui s'allument
        int pos = random16(NUM_LEDS);        
        
        // On choisit une luminosité aléatoire entre 100 et 255
        uint8_t randomBrightness = random8(0, 170);
        
        // CHSV(Teinte, Saturation, Valeur)
        // Saturation 0 = Blanc pur
        leds[pos] = CHSV(0, 0, randomBrightness);
    }    
}