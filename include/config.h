#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- RÉSEAU ESP-NOW ---
#define WIFI_CHANNEL 1
#define TX_POWER_LEVEL 15
const uint8_t BROADCAST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// --- REDONDANCE ---
#define MSG_REDUNDANCY_COUNT 3
#define MSG_REDUNDANCY_DELAY 4

// --- TIMING ---
// Le master envoie toutes les 2000 ms, donc on met un timeout plus long
const unsigned long SIGNAL_TIMEOUT = 2600;

// --- HOPS ---
// 0 = reçu directement du master
// 1 = via 1 intermédiaire
// 2 = via 2 intermédiaires
// >= 3 = violet
// Ajuste cette valeur si tu veux limiter davantage la propagation
#define MAX_HOPS 6

// --- HARDWARE ---
#define LED_PIN 3
#define NUM_LEDS 32

// Limitation de puissance FastLED
#define LED_POWER_VOLTAGE 5
#define LED_POWER_MILLIAMPS 400

// --- STRUCTURE COMMUNE ---
typedef struct struct_message {
    uint32_t msgId;
    uint8_t hopCount;
} struct_message;

#endif