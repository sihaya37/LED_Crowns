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
const unsigned long MASTER_HEARTBEAT_INTERVAL = 2000;
const unsigned long EFFECT_CHANGE_INTERVAL = 10000;

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

// --- PROTOCOLE ---
#define PROTOCOL_VERSION 2
#define CROWN_SYNC_DELAY 1000

enum MessageCommand : uint8_t {
    COMMAND_HEARTBEAT = 0,
    COMMAND_ACTIVATE_CROWN = 1
};

enum EffectId : uint8_t {
    EFFECT_OFF = 0,
    EFFECT_DEBUG_HOPS = 1,
    EFFECT_SOLID = 2,
    EFFECT_BREATH = 3,
    EFFECT_CORONATION = 4,
    EFFECT_SPARKLE = 5,
    EFFECT_WAVE = 6,
    EFFECT_AURORA = 7,
    EFFECT_COMET_TWINS = 8,
    EFFECT_HEARTBEAT = 9,
    EFFECT_COLOR_CHASE = 10,
    EFFECT_THEATER_CHASE = 11,
    EFFECT_PORTAL = 12,
    EFFECT_FIREWORKS = 13,
    EFFECT_GLITTER_RAIN = 14,
    EFFECT_LARSON_SCANNER = 15,
    EFFECT_PRISM = 16,
    EFFECT_RIPPLE = 17,
    EFFECT_CONSTELLATION = 18,
    EFFECT_VOGUE_POSE = 19,
    EFFECT_STORM = 20,
    EFFECT_QUEEN_AURA = 21
};

// --- STRUCTURE COMMUNE ---
typedef struct __attribute__((packed)) struct_message {
    uint8_t protocolVersion;
    uint8_t command;
    uint8_t effectId;
    uint8_t hopCount;
    uint8_t intensity;
    uint8_t speed;
    uint16_t flags;
    uint32_t sessionId;
    uint32_t msgId;
    uint32_t primaryColor;
    uint32_t secondaryColor;
    uint8_t targetMac[6];
} struct_message;

#endif
