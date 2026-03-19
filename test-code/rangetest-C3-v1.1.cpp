// Test validé avec C3 n°3 comme master, A et B comme nodes.
// C sur mon bureau, B vers porte sdb, C porte de la chambre : B flash 1 fois bleu et A flash 2 fois vert
// Validé aussi avec le WROOM en master et A, B et C en nodes : la portée du WROOM est nettement meilleure.

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>

// --- Configuration Matérielle ---
#define LED_PIN     3    
#define NUM_LEDS    32   
#define BRIGHTNESS  50
CRGB leds[NUM_LEDS];

// --- Variables de contrôle ---
typedef struct struct_message {
    uint32_t msgId;
    uint8_t hopCount;
    int8_t txPower; 
} struct_message;

struct_message incomingRead;
uint32_t lastProcessedId = 0;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Chronomètres pour la fluidité visuelle
unsigned long lastSignalTime = 0;
const unsigned long silenceAfterFlash = 600;  // Temps de noir forcé après les flashs
const unsigned long waitBeforeRed = 1600;    // Ton souhait : 1.1s avant le retour du rouge

void flash(CRGB color, int times) {
    for(int i=0; i<times; i++) {
        fill_solid(leds, NUM_LEDS, color);
        FastLED.show();
        delay(150); 
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        delay(150);
    }
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    memcpy(&incomingRead, incomingData, sizeof(incomingRead));

    if (incomingRead.msgId <= lastProcessedId) return;
    lastProcessedId = incomingRead.msgId;
    
    // On note l'instant T de la réception
    lastSignalTime = millis();

    if (incomingRead.hopCount == 0) {
        flash(CRGB::Blue, 1); // Direct
        incomingRead.hopCount++; 
        esp_now_send(broadcastAddress, (uint8_t *) &incomingRead, sizeof(incomingRead));
    } else {
        flash(CRGB::Green, 2); // Relayé
    }
    
    // On force le noir immédiatement après l'animation
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) return;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void loop() {
    unsigned long now = millis();
    unsigned long timeSinceSignal = now - lastSignalTime;

    if (timeSinceSignal < silenceAfterFlash) {
        // 1. Phase de "Noir" juste après le flash
        fill_solid(leds, NUM_LEDS, CRGB::Black);
    } 
    else if (timeSinceSignal < waitBeforeRed) {
        // 2. Phase d'attente : Toujours noir jusqu'à 1100ms
        fill_solid(leds, NUM_LEDS, CRGB::Black);
    } 
    else {
        // 3. Retour au rouge fixe après 1.1s de calme radio
        fill_solid(leds, NUM_LEDS, CRGB(15, 0, 0)); 
    }

    FastLED.show();
    delay(20); 
}