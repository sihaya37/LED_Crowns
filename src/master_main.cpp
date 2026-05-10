#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <MFRC522.h>
#include "config.h"

#define RST_PIN 22
#define SS_PIN  21

MFRC522 rfid(SS_PIN, RST_PIN);

struct_message myData;
uint32_t sessionId = 0;
uint32_t msgCounter = 0;
uint8_t currentDemoEffect = 0;

struct PendingSend {
    struct_message message;
    uint8_t remaining;
    unsigned long nextSendAt;
    bool active;
};

PendingSend pendingSend = {};

struct DemoEffect {
    uint8_t effectId;
    const char* name;
    uint8_t intensity;
    uint8_t speed;
    uint32_t primaryColor;
    uint32_t secondaryColor;
};

const DemoEffect demoEffects[] = {
    {EFFECT_SOLID, "SOLID warm gold", 150, 0, 0xFFD060, 0x000000},
    {EFFECT_BREATH, "BREATH deep red", 180, 18, 0xB00020, 0x000000},
    {EFFECT_CORONATION, "CORONATION gold rise", 210, 205, 0xFFD060, 0x100300},
    {EFFECT_SPARKLE, "SPARKLE white gold", 190, 70, 0xFFF2B0, 0x000000},
    {EFFECT_WAVE, "WAVE queen magenta", 180, 35, 0xFF20A8, 0x080010},
    {EFFECT_AURORA, "AURORA pink blue violet", 170, 44, 0xFF4FD8, 0x103BFF},
    {EFFECT_COMET_TWINS, "COMET TWINS magenta cyan", 210, 48, 0xFF1FBF, 0x16D7FF},
    {EFFECT_HEARTBEAT, "HEARTBEAT hot pink", 210, 46, 0xFF006E, 0x210018},
    {EFFECT_COLOR_CHASE, "COLOR CHASE royal blue", 190, 92, 0x2274FF, 0x7D1AFF},
    {EFFECT_THEATER_CHASE, "THEATER CHASE violet", 185, 105, 0xD92CFF, 0x09001F},
    {EFFECT_PORTAL, "PORTAL blue violet", 205, 92, 0x5F7CFF, 0xD100FF},
    {EFFECT_FIREWORKS, "FIREWORKS rose gold", 210, 78, 0xFF2FA6, 0xFFD15C},
    {EFFECT_GLITTER_RAIN, "GLITTER RAIN ice pink", 185, 80, 0xFFD6F6, 0x173CFF},
    {EFFECT_LARSON_SCANNER, "LARSON SCANNER fuchsia", 220, 55, 0xFF004C, 0x020008},
    {EFFECT_PRISM, "PRISM saturated rainbow", 170, 32, 0xFF2BD6, 0x1C4DFF},
    {EFFECT_RIPPLE, "RIPPLE blue pink", 205, 76, 0x1E90FF, 0xFF4FD8},
    {EFFECT_CONSTELLATION, "CONSTELLATION violet stars", 210, 38, 0xF8E8FF, 0x25004D},
    {EFFECT_VOGUE_POSE, "VOGUE POSE sharp magenta", 215, 112, 0xFF1FBF, 0x1212A8},
    {EFFECT_STORM, "STORM electric blue", 220, 86, 0x76E8FF, 0x230060},
    {EFFECT_QUEEN_AURA, "QUEEN AURA pink violet", 190, 28, 0xFF6AD5, 0x5E2CFF}
};

const uint8_t DEMO_EFFECT_COUNT = sizeof(demoEffects) / sizeof(demoEffects[0]);

const char* effectName(uint8_t index) {
    if (index >= DEMO_EFFECT_COUNT) {
        return "UNKNOWN";
    }
    return demoEffects[index].name;
}

void scheduleBroadcast(const struct_message& message) {
    pendingSend.message = message;
    pendingSend.remaining = MSG_REDUNDANCY_COUNT;
    pendingSend.nextSendAt = millis();
    pendingSend.active = true;
}

void processBroadcastQueue() {
    if (!pendingSend.active || pendingSend.remaining == 0) {
        pendingSend.active = false;
        return;
    }

    unsigned long now = millis();
    if ((long)(now - pendingSend.nextSendAt) < 0) {
        return;
    }

    esp_now_send(BROADCAST_ADDR, (uint8_t*)&pendingSend.message, sizeof(pendingSend.message));
    pendingSend.remaining--;
    pendingSend.nextSendAt = now + MSG_REDUNDANCY_DELAY;

    if (pendingSend.remaining == 0) {
        pendingSend.active = false;
    }
}

void prepareMessage(const DemoEffect& effect) {
    msgCounter++;

    myData.protocolVersion = PROTOCOL_VERSION;
    myData.effectId = effect.effectId;
    myData.hopCount = 0;
    myData.intensity = effect.intensity;
    myData.speed = effect.speed;
    myData.flags = 0;
    myData.sessionId = sessionId;
    myData.msgId = msgCounter;
    myData.primaryColor = effect.primaryColor;
    myData.secondaryColor = effect.secondaryColor;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- MASTER START ---");
    sessionId = esp_random();

    SPI.begin();
    rfid.PCD_Init();

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur ESP-NOW init");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDR, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Erreur ajout peer broadcast");
        return;
    }

    WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4));
    Serial.printf("Master pret. Canal=%d, TX=%d, Session=%u\n", WIFI_CHANNEL, TX_POWER_LEVEL, sessionId);
}

void loop() {
    static unsigned long lastSend = 0;
    static unsigned long lastEffectChange = 0;
    unsigned long now = millis();

    processBroadcastQueue();

    if (now - lastEffectChange >= EFFECT_CHANGE_INTERVAL) {
        lastEffectChange = now;
        currentDemoEffect = (currentDemoEffect + 1) % DEMO_EFFECT_COUNT;
        lastSend = 0;
        Serial.printf("\n--- Changement effet: %s (%u/%u) ---\n",
            effectName(currentDemoEffect),
            currentDemoEffect + 1,
            DEMO_EFFECT_COUNT);
    }

    if (lastSend == 0 || now - lastSend >= MASTER_HEARTBEAT_INTERVAL) {
        lastSend = now;

        const DemoEffect& effect = demoEffects[currentDemoEffect];
        prepareMessage(effect);
        scheduleBroadcast(myData);

        Serial.printf(
            "Send msg=%u session=%u effect=%s id=%u intensity=%u speed=%u primary=#%06X secondary=#%06X\n",
            msgCounter,
            sessionId,
            effect.name,
            effect.effectId,
            effect.intensity,
            effect.speed,
            effect.primaryColor,
            effect.secondaryColor);
    }
}
