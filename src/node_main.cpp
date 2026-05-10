#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "effects.h"

uint32_t lastSessionId = 0;
uint32_t lastProcessedId = 0;
bool hasSession = false;
unsigned long lastEffectUpdate = 0;

struct PendingRelay {
    struct_message message;
    uint8_t remaining;
    unsigned long nextSendAt;
    bool active;
};

PendingRelay pendingRelay = {};

void scheduleRelay(const struct_message& message) {
    pendingRelay.message = message;
    pendingRelay.remaining = MSG_REDUNDANCY_COUNT;
    pendingRelay.nextSendAt = millis();
    pendingRelay.active = true;
}

void processRelayQueue() {
    if (!pendingRelay.active || pendingRelay.remaining == 0) {
        pendingRelay.active = false;
        return;
    }

    unsigned long now = millis();
    if ((long)(now - pendingRelay.nextSendAt) < 0) {
        return;
    }

    WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4));
    esp_now_send(BROADCAST_ADDR, (uint8_t*)&pendingRelay.message, sizeof(pendingRelay.message));
    pendingRelay.remaining--;
    pendingRelay.nextSendAt = now + MSG_REDUNDANCY_DELAY;

    if (pendingRelay.remaining == 0) {
        pendingRelay.active = false;
    }
}

void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(struct_message)) {
        return;
    }

    struct_message incoming;
    memcpy(&incoming, data, sizeof(incoming));

    if (incoming.protocolVersion != PROTOCOL_VERSION) {
        return;
    }

    if (!hasSession || incoming.sessionId != lastSessionId) {
        hasSession = true;
        lastSessionId = incoming.sessionId;
        lastProcessedId = 0;
    }

    if (incoming.msgId <= lastProcessedId) {
        return;
    }

    lastProcessedId = incoming.msgId;
    effectsApplyMessage(incoming);

    if (incoming.hopCount < MAX_HOPS) {
        incoming.hopCount++;
        scheduleRelay(incoming);
    }
}

void setup() {
    Serial.begin(115200);

    effectsInit();

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

    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

    WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4));
    Serial.printf("Node pret. Canal=%d, TX=%d\n", WIFI_CHANNEL, TX_POWER_LEVEL);
}

void loop() {
    unsigned long now = millis();

    processRelayQueue();

    if (now - lastEffectUpdate >= 20) {
        lastEffectUpdate = now;
        effectsUpdate(now);
    }
}
