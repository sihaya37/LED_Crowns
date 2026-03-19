// Test validé avec C3 n°3 comme master, A et B comme nodes.
// C sur mon bureau, B vers porte sdb, C porte de la chambre : B flash 1 fois bleu et A flash 2 fois vert
// Validé aussi avec le WROOM en master et A, B et C en nodes : la portée du WROOM est nettement meilleure.

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

typedef struct struct_message {
    uint32_t msgId;
    uint8_t hopCount;
    int8_t txPower; 
} struct_message;

struct_message myData;
uint32_t counter = 0;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void setup() {
    Serial.begin(115200);
    
    WiFi.mode(WIFI_STA);
    
    // Fixe le canal 1 pour éviter que l'ESP-NOW ne dérive
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
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Erreur Peer");
        return;
    }
    
    Serial.println("Master C3 opérationnel - Envoi toutes les 2s");
}

void loop() {
    // Puissance d'émission (11 dBm est un bon compromis batterie/portée)
    int8_t currentPwr = 11; 
    WiFi.setTxPower((wifi_power_t)currentPwr); 
    
    counter++;
    myData.msgId = counter;
    myData.hopCount = 0; // Source originale
    myData.txPower = currentPwr;

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
    
    if (result == ESP_OK) {
        Serial.printf("ID %u envoyé.\n", counter);
    }

    // Nouvelle cadence de 2 secondes
    delay(2000); 
}