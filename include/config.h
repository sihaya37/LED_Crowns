#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- RÉSEAU ESP-NOW ---
#define WIFI_CHANNEL 1
#define TX_POWER_LEVEL 15 // Palier validé à 15
const uint8_t BROADCAST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// --- REDONDANCE ---
#define MSG_REDUNDANCY_COUNT 3
#define MSG_REDUNDANCY_DELAY 4

// --- HARDWARE ---
#define LED_PIN 3    // Pin validé pour C3
#define NUM_LEDS 32  // À ajuster selon tes couronnes

// --- STRUCTURE COMMUNE ---
typedef struct struct_message {
    uint32_t msgId;
    uint8_t effectMode; // 1: Scintillement, 2: Dégradé, 3: Battement
    uint8_t hopCount;   // <--- AJOUT : Compteur de sauts pour le relais
} struct_message;

#endif