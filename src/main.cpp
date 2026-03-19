#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

#define BOOT_BUTTON 0 

// --- Structure de données (Identique aux Nodes) ---
typedef struct struct_message {
    uint32_t msgId;
    uint8_t hopCount;
    int8_t txPower; 
} struct_message;

struct_message myData;
uint32_t counter = 0;
const int8_t FIXED_PWR_PALIER = 15; // Puissance de croisière validée
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void setup() {
    Serial.begin(115200);
    delay(2000); 
    
    Serial.println("\n*******************************");
    Serial.println("MASTER WROOM : MODE REDONDANCE");
    Serial.println("*******************************");

    WiFi.mode(WIFI_STA);
    
    // Verrouillage Canal 1
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur ESP-NOW");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.printf("Initialisation OK. Puissance fixe : Palier %d\n", FIXED_PWR_PALIER);
}

void loop() {
    static unsigned long lastSend = 0;
    
    // Envoi toutes les 2 secondes
    if (millis() - lastSend > 2000) {
        lastSend = millis();
        counter++;
        
        myData.msgId = counter;
        myData.hopCount = 0;
        myData.txPower = FIXED_PWR_PALIER;

        // Réglage de la puissance avant l'envoi
        WiFi.setTxPower((wifi_power_t)(FIXED_PWR_PALIER * 4)); 

        // --- ENVOI AVEC REDONDANCE (Le Perroquet Master) ---
        for(int i = 0; i < 3; i++) {
            esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
            delay(4); // Petit délai pour ne pas saturer le buffer radio
        }

        Serial.printf("[%u] Message envoyé 3 fois (Redondance active)\n", counter);
    }
}