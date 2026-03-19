#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// --- Structure de données (Identique aux Nodes) ---
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
    
    // Initialisation WiFi
    WiFi.mode(WIFI_STA);
    
    // Verrouillage Canal 1
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // Initialisation ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur initialisation ESP-NOW");
        return;
    }

    // Enregistrement du Peer (Broadcast)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;  
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Erreur ajout du Peer");
        return;
    }
    
    Serial.println("Master C3 prêt - Test de silence 10s");
}

void loop() {
    // On fixe une puissance moyenne pour le test
    int8_t currentPwr = 11; 
    WiFi.setTxPower((wifi_power_t)currentPwr); 
    
    counter++;
    myData.msgId = counter;
    myData.hopCount = 0;    // Le Master est toujours au saut 0
    myData.txPower = currentPwr;

    // Envoi du message
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
    
    if (result == ESP_OK) {
        Serial.printf("ID %u envoyé à %d dBm. Silence radio (10s)...\n", counter, currentPwr);
    } else {
        Serial.println("Erreur lors de l'envoi");
    }

    // Délai massif pour laisser les Nodes relayer sans interférence
    delay(10000); 
}