// Version TEST RAMPE BROADCAST
// Node A : Relaye avec un délai progressif (msgId * 10ms)
// Node B : Écoute et logge les doublons avec un double flash vert
// VALIDE : B reçoit bien le message d'origine du master ET le doublon relayé par A dès la première itération
// Ce qui semble avoir réglé le souci :
// - Utilisation du Broadcast plutôt que l'Unicast+ enregistrement explicite du peer FF:FF:FF:FF:FF:FF lors du setup.
// - Suppression des délais bloquants, utilisation d'une structure asynchrone avec millis()
// - Verrouillage sur Channel 1

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>

#define LED_PIN     3
#define NUM_LEDS    32
CRGB leds[NUM_LEDS];

// --- ADRESSES MAC POUR IDENTIFICATION ---
uint8_t macNodeA[]  = {0x3C, 0xDC, 0x75, 0x33, 0x78, 0x74};

typedef struct struct_message {
    uint32_t msgId;
    uint8_t hopCount;
    int8_t txPower; 
} struct_message;

// Variables de contrôle
uint32_t lastProcessedMsgId = 0;
bool isNodeA = false;
bool pendingRelay = false;
uint32_t targetSendTime = 0;
struct_message relayDataBuffer;

// Variables pour l'animation des LEDs
int ledEffect = 0; // 0: off, 1: bleu simple, 2: vert double
unsigned long effectStartTime = 0;

void setup() {
    Serial.begin(115200);
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    
    WiFi.mode(WIFI_STA);
    uint8_t myMac[6];
    WiFi.macAddress(myMac);

    // On détermine qui est qui pour le test
    if (memcmp(myMac, macNodeA, 6) == 0) {
        isNodeA = true;
        Serial.println("--- ROLE : NODE A (EMETTEUR DU RELAIS) ---");
    } else {
        isNodeA = false;
        Serial.println("--- ROLE : NODE B (RECEPTEUR / OBSERVATEUR) ---");
    }

    // Config radio STRICTEMENT identique au Master
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) return;

    // TOUT LE MONDE ajoute l'adresse de Broadcast (FF:FF:FF:FF:FF:FF)
    // C'est la clé : Node B acceptera ainsi n'importe quel message broadcast
    uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    // Callback de réception
    esp_now_register_recv_cb(esp_now_recv_cb_t([](const uint8_t *mac, const uint8_t *data, int len) {
        struct_message incoming;
        memcpy(&incoming, data, sizeof(incoming));

        // CAS 1 : Signal direct (Master ou 1er relais capté)
        if (incoming.msgId > lastProcessedMsgId) {
            lastProcessedMsgId = incoming.msgId;
            Serial.printf("\n[ID %u] Signal direct reçu (Hops: %u)\n", incoming.msgId, incoming.hopCount);
            
            ledEffect = 1; // Flash Bleu
            effectStartTime = millis();

            if (isNodeA) {
                // Node A : Calcul de la rampe (ID * 10ms)
                uint32_t wait = incoming.msgId * 10;
                relayDataBuffer = incoming;
                relayDataBuffer.hopCount++;
                targetSendTime = millis() + wait;
                pendingRelay = true;
                Serial.printf("Node A : Relais BROADCAST programmé dans %u ms\n", wait);
            }
        } 
        // CAS 2 : Doublon (Uniquement pour Node B)
        else if (incoming.msgId == lastProcessedMsgId && !isNodeA) {
            Serial.printf("[ID %u] DOUBLON reçu ! (Hops: %u)\n", incoming.msgId, incoming.hopCount);
            
            ledEffect = 2; // Double Flash Vert
            effectStartTime = millis();
        }
    }));
}

void loop() {
    unsigned long now = millis();

    // 1. Gestion du relais (Node A seulement)
    if (isNodeA && pendingRelay && now >= targetSendTime) {
        uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_now_send(broadcastAddress, (uint8_t *) &relayDataBuffer, sizeof(relayDataBuffer));
        pendingRelay = false;
        Serial.println("Node A : Relais envoyé en Broadcast !");

        // ON AJOUTE LE DOUBLE FLASH VERT SUR A ICI ✅
        ledEffect = 2; 
        effectStartTime = millis();
    }

    // 2. Gestion des animations LEDs (non-bloquantes)
    if (ledEffect == 1) { // Flash Bleu (Réception)
        if (now - effectStartTime < 200) fill_solid(leds, NUM_LEDS, CRGB::Blue);
        else { FastLED.clear(); ledEffect = 0; }
    } 
    else if (ledEffect == 2) { // Double Flash Vert (Relais envoyé pour A / Doublon reçu pour B)
        unsigned long elapsed = now - effectStartTime;
        if (elapsed < 150) fill_solid(leds, NUM_LEDS, CRGB::Green);
        else if (elapsed < 300) FastLED.clear();
        else if (elapsed < 450) fill_solid(leds, NUM_LEDS, CRGB::Green);
        else { FastLED.clear(); ledEffect = 0; }
    }

    FastLED.show();
}