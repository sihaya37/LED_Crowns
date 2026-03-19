// Permet de faire varier la puissance d'émission de chaque node par appui sur son bouton boot, afin de tester la portée en fonction de la puissance.
// Au palier 15 le passage de relais entre les nodes semble se faire de manière assez stable et avec une portée supérieure à 15m.
// Test réussi avec le master sur mon bureau, le node A au sol près de la porte d'entre de la maison et le node B à côté de l'évier dans la cuisine.
// LE node B reçoit le signal de A de façon très stable malgré la distance et les obstacles.

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>

#define LED_PIN     3
#define NUM_LEDS    32
#define BOOT_BUTTON 9 // Bouton BOOT sur ESP32-C3
CRGB leds[NUM_LEDS];

typedef struct struct_message {
    uint32_t msgId;
    uint8_t hopCount;
    int8_t txPower; 
} struct_message;

struct_message incomingRead;
uint32_t lastProcessedId = 0;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Variables de test
int8_t currentPwr = 1; 
unsigned long lastSignalTime = 0;
const unsigned long waitBeforeRed = 1600;

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

void OnDataRecv(const uint8_t * mac, const uint8_t *data, int len) {
    memcpy(&incomingRead, data, sizeof(incomingRead));
    if (incomingRead.msgId <= lastProcessedId) return;
    lastProcessedId = incomingRead.msgId;
    lastSignalTime = millis();

    if (incomingRead.hopCount == 0) {
        flash(CRGB::Blue, 1); // Direct
    } else if (incomingRead.hopCount == 1) {
        flash(CRGB::Green, 2); // 1 Intermédiaire
    } else if (incomingRead.hopCount == 2) {
        flash(CRGB::Orange, 3); // 2 Intermédiaires
    }

    // Relais si hopCount < 2
    if (incomingRead.hopCount < 2) {
        incomingRead.hopCount++;
        WiFi.setTxPower((wifi_power_t)(currentPwr * 4)); // Conversion approximative dBm
        esp_now_send(broadcastAddress, (uint8_t *) &incomingRead, sizeof(incomingRead));
    }
    
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50);

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    esp_now_init();
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;
    esp_now_add_peer(&peerInfo);
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void loop() {
    // 1. Lecture du bouton (simplifiée pour le test)
    if (digitalRead(BOOT_BUTTON) == LOW) {
        currentPwr++;
        if (currentPwr > 20) currentPwr = 1;
        Serial.printf("Puissance réglée à : %d\n", currentPwr);
        delay(300); // Anti-rebond grossier
    }

    // 2. Affichage de la puissance (LEDs blanches)
    if (millis() - lastSignalTime > waitBeforeRed) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        for(int i=0; i < currentPwr; i++) {
            leds[i] = CRGB(15, 0, 0); // Veilleuse rouge, le nombre de leds = puissance
        }
    }
    FastLED.show();
}