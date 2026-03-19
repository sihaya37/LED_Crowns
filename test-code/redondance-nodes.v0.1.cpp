// Suite aux tests "wifipower" on a décidé de fixer la puissance d'émission au palier 15.
// Ici on ajoute une redondance de 3 envois pour chaque relais, afin de compenser les pertes de paquets et d'améliorer la fiabilité du passage de relais.

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

// --- Structure de données ---
typedef struct struct_message {
    uint32_t msgId;
    uint8_t hopCount;
    int8_t txPower; 
} struct_message;

struct_message incomingRead;
uint32_t lastProcessedId = 0;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// --- Paramètres de Redondance & Timing ---
const int8_t FIXED_PWR_PALIER = 15; // Ton réglage validé
unsigned long lastSignalTime = 0;
const unsigned long waitBeforeRed = 1600; 

// --- Fonction de Flash ---
void flash(CRGB color, int times) {
    for(int i=0; i<times; i++) {
        fill_solid(leds, NUM_LEDS, color);
        FastLED.show();
        delay(120); // Un peu plus nerveux pour la réactivité
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        delay(100);
    }
}

// --- Callback de Réception (Le Perroquet) ---
void OnDataRecv(const uint8_t * mac, const uint8_t *data, int len) {
    memcpy(&incomingRead, data, sizeof(incomingRead));

    // 1. Filtre anti-doublon (Crucial avec la redondance !)
    if (incomingRead.msgId <= lastProcessedId) return;
    lastProcessedId = incomingRead.msgId;
    lastSignalTime = millis();

    // 2. Action Visuelle
    if (incomingRead.hopCount == 0) flash(CRGB::Blue, 1);
    else if (incomingRead.hopCount == 1) flash(CRGB::Green, 2);
    else if (incomingRead.hopCount == 2) flash(CRGB::Orange, 3);

    // 3. Relais avec Redondance (3 envois successifs)
    if (incomingRead.hopCount < 2) {
        incomingRead.hopCount++;
        
        // On force la puissance au Palier 15
        WiFi.setTxPower((wifi_power_t)(FIXED_PWR_PALIER * 4)); 

        for(int i = 0; i < 3; i++) {
            esp_now_send(broadcastAddress, (uint8_t *) &incomingRead, sizeof(incomingRead));
            delay(4); // Laisse le temps à la pile radio de traiter chaque paquet
        }
        Serial.printf("Relais ID %u Perroquet OK (Hops: %u)\n", incomingRead.msgId, incomingRead.hopCount);
    }
    
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
    esp_now_add_peer(&peerInfo);

    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    Serial.println("Node REDONDANCE prêt (Palier 15)");
}

void loop() {
    // Veilleuse rouge fixe après 1.6s sans signal
    if (millis() - lastSignalTime > waitBeforeRed) {
        fill_solid(leds, NUM_LEDS, CRGB(15, 0, 0)); 
    } else {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
    }
    FastLED.show();
    delay(20); 
}