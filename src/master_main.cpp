#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <MFRC522.h>
#include "config.h" 

// --- Configuration Pins RC522 ---
#define RST_PIN   22 
#define SS_PIN    21 

MFRC522 mfrc522(SS_PIN, RST_PIN);

// --- Variables d'État ---
uint8_t currentEffect = 1;      
uint8_t effectBeforeFlash = 1;  
unsigned long flashStartTime = 0;
bool isFlashing = false;

struct_message myData;
uint32_t msgCounter = 0;

// Fonction pour envoyer l'état actuel aux couronnes avec redondance
void broadcastEffect(uint8_t effectId) {
    msgCounter++;
    myData.msgId = msgCounter;
    myData.effectMode = effectId; 
    myData.hopCount = 0; // Réinitialise le compteur de sauts pour un nouveau message

    // Utilisation des constantes de config.h pour la redondance
    for(int i = 0; i < MSG_REDUNDANCY_COUNT; i++) {
        esp_now_send(BROADCAST_ADDR, (uint8_t *) &myData, sizeof(myData));
        delay(MSG_REDUNDANCY_DELAY); 
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n--- MASTER WROOM : READY ---");

    // 1. Initialisation SPI et RFID
    SPI.begin();
    mfrc522.PCD_Init();
    Serial.println("Lecteur RC522 : OK");

    // 2. Configuration WiFi (Canal défini dans config.h)
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // Réglage de la puissance d'émission
    WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4)); 

    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur ESP-NOW");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDR, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void loop() {
    // --- GESTION DU TEMPS (Effet 3 : 5 secondes) ---
    if (isFlashing && (millis() - flashStartTime > 5000)) {
        isFlashing = false;
        currentEffect = effectBeforeFlash;
        Serial.println("Fin Effet 3 -> Retour");
        broadcastEffect(currentEffect);
    }

    // --- LECTURE DU BADGE ---
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String uid = "";
        for (byte i = 0; i < mfrc522.uid.size; i++) {
            uid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
            uid += String(mfrc522.uid.uidByte[i], HEX);
        }
        uid.toUpperCase();
        Serial.print("UID : "); Serial.println(uid);

        if (uid == "E96BDC6E") { // TAG 1
            if (!isFlashing) {
                currentEffect = (currentEffect == 1) ? 2 : 1;
            } else {
                effectBeforeFlash = (effectBeforeFlash == 1) ? 2 : 1;
            }
        } 
        else if (uid == "53269BE4") { // TAG 2
            if (!isFlashing) {
                effectBeforeFlash = currentEffect;
                isFlashing = true;
            }
            currentEffect = 3;
            flashStartTime = millis();
        }

        broadcastEffect(currentEffect);
        mfrc522.PICC_HaltA();
    }

    // --- PULSATION DE SÉCURITÉ (Toutes les secondes) ---
    static unsigned long lastPulse = 0;
    if (millis() - lastPulse > 1000) {
        broadcastEffect(currentEffect);
        lastPulse = millis();
    }
}