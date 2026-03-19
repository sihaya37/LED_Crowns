// Permet de faire varier la puissance d'émission du WROOM en fonction d'un bouton, et d'envoyer des messages ESP-NOW avec cette puissance, afin de tester la portée en fonction de la puissance. 
// Avec le master sur mon bureau au palier 2, la réception devient instable en bas de l'escalier / vers la gamelle des chats.
// Au palier 20, la réception est stable jusqu'à la porte d'entrée.
// On va opter pour le palier 15 qui est un bon compromis.
// Si nécessaire pour avoir des effets lumineux de contagion on cherchera à faire varier ces puissances.

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

#define BOOT_BUTTON 0 

typedef struct struct_message {
    uint32_t msgId;
    uint8_t hopCount;
    int8_t txPower; 
} struct_message;

struct_message myData;
uint32_t counter = 0;
int8_t currentPwr = 1; 
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void setup() {
    // 1. Initialisation série avec attente
    Serial.begin(115200);
    delay(2000); // Laisse le temps au port COM de se stabiliser
    
    Serial.println("\n--- DEMARRAGE MASTER WROOM ---");
    
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    
    WiFi.mode(WIFI_STA);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur ESP-NOW");
        return;
    }
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.println("Initialisation OK. Prêt à émettre.");
}

void loop() {
    // 2. Gestion bouton avec affichage immédiat
    if (digitalRead(BOOT_BUTTON) == LOW) {
        currentPwr++;
        if (currentPwr > 20) currentPwr = 1;
        
        Serial.print(">>> BOUTON PRESSE - Nouveau palier : ");
        Serial.println(currentPwr);
        
        // Anti-rebond : attend que le bouton soit relâché
        while(digitalRead(BOOT_BUTTON) == LOW) { delay(10); }
        delay(100); 
    }

    // 3. Envoi toutes les 2 secondes (non bloquant pour le bouton)
    static unsigned long lastSend = 0;
    if (millis() - lastSend > 2000) {
        lastSend = millis();
        counter++;
        myData.msgId = counter;
        myData.hopCount = 0;
        myData.txPower = currentPwr;

        // Conversion palier 1-20 vers puissance réelle
        WiFi.setTxPower((wifi_power_t)(currentPwr * 4)); 
        
        esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
        
        Serial.printf("[%u] Envoi | Palier: %d | TX: %d\n", counter, currentPwr, currentPwr * 4);
    }
}