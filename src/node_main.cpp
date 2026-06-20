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
bool crownActivated = false;

uint8_t localMac[6] = {};

const uint8_t TOTEM_MACS[][6] = {
    {0xAC, 0xEB, 0xE6, 0x6E, 0x87, 0x94}, // H
    {0x08, 0x92, 0x72, 0x25, 0x47, 0x80}, // J
    {0x08, 0x92, 0x72, 0x25, 0x62, 0x98}, // N
    {0x3C, 0xDC, 0x75, 0x33, 0x80, 0x08}  // S
};

const uint8_t TOTEM_COUNT = sizeof(TOTEM_MACS) / sizeof(TOTEM_MACS[0]);

struct PendingRelay {
    struct_message message;
    uint8_t remaining;
    unsigned long nextSendAt;
    bool active;
};

PendingRelay pendingRelay = {};

struct PendingSync {
    struct_message message;
    unsigned long applyAt;
    bool active;
};

PendingSync pendingSync = {};

struct PendingActivation {
    struct_message message;
    unsigned long applyAt;
    bool active;
};

PendingActivation pendingActivation = {};

struct PendingStrobe {
    unsigned long applyAt;
    bool active;
};

PendingStrobe pendingStrobe = {};

bool isTargetCrown(const struct_message& message) {
    return memcmp(message.targetMac, localMac, sizeof(localMac)) == 0;
}

bool isTotemNode() {
    for (uint8_t i = 0; i < TOTEM_COUNT; i++) {
        if (memcmp(localMac, TOTEM_MACS[i], sizeof(localMac)) == 0) {
            return true;
        }
    }

    return false;
}

void applyTotemIdle() {
    struct_message idle = {};
    idle.protocolVersion = PROTOCOL_VERSION;
    idle.command = COMMAND_GLOBAL_EFFECT;
    idle.effectId = EFFECT_SOLID;
    idle.hopCount = 0;
    idle.intensity = TOTEM_IDLE_INTENSITY;
    idle.speed = 0;
    idle.flags = 0;
    idle.sessionId = lastSessionId;
    idle.msgId = lastProcessedId;
    idle.primaryColor = TOTEM_IDLE_COLOR;
    idle.secondaryColor = 0x000000;

    effectsSetActive(true);
    effectsApplyMessage(idle);
}

void scheduleGroupStrobe() {
    pendingStrobe.applyAt = millis() + YARA_TO_CROWN_ACTIVATION_DELAY + ACTIVATION_SPARKLE_DURATION;
    pendingStrobe.active = true;
}

void scheduleEffectSync(const struct_message& message) {
    pendingSync.message = message;
    pendingSync.applyAt = millis() + YARA_TO_CROWN_ACTIVATION_DELAY + CROWN_SYNC_DELAY;
    pendingSync.active = true;
}

void scheduleActivation(const struct_message& message) {
    pendingActivation.message = message;
    pendingActivation.applyAt = millis() + YARA_TO_CROWN_ACTIVATION_DELAY;
    pendingActivation.active = true;
}

void processGroupStrobe() {
    if (!pendingStrobe.active) {
        return;
    }

    unsigned long now = millis();
    if ((long)(now - pendingStrobe.applyAt) < 0) {
        return;
    }

    pendingStrobe.active = false;
    effectsStartGroupStrobe();
}

void processActivation() {
    if (!pendingActivation.active) {
        return;
    }

    unsigned long now = millis();
    if ((long)(now - pendingActivation.applyAt) < 0) {
        return;
    }

    pendingActivation.active = false;
    crownActivated = true;
    effectsSetBlackout(false);
    effectsSetActive(true);
    effectsStartActivation(pendingActivation.message);
}

void processEffectSync() {
    if (!pendingSync.active) {
        return;
    }

    unsigned long now = millis();
    if ((long)(now - pendingSync.applyAt) < 0) {
        return;
    }

    pendingSync.active = false;
    effectsStartSync(pendingSync.message);
}

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

    switch (incoming.command) {
        case COMMAND_ACTIVATE_CROWN:
            if (isTargetCrown(incoming)) {
                pendingActivation.active = false;
                pendingSync.active = false;
                pendingStrobe.active = false;
                scheduleActivation(incoming);
            } else if (crownActivated) {
                scheduleGroupStrobe();
                scheduleEffectSync(incoming);
            }
            break;

        case COMMAND_GLOBAL_EFFECT:
        case COMMAND_TEST_NETWORK:
            crownActivated = true;
            pendingActivation.active = false;
            pendingSync.active = false;
            pendingStrobe.active = false;
            effectsSetBlackout(false);
            effectsSetActive(true);
            effectsApplyMessage(incoming);
            break;

        case COMMAND_BLACKOUT:
            pendingActivation.active = false;
            pendingSync.active = false;
            pendingStrobe.active = false;
            effectsSetBlackout(true);
            break;

        case COMMAND_RESTORE_FROM_BLACKOUT:
            pendingActivation.active = false;
            pendingSync.active = false;
            pendingStrobe.active = false;
            effectsSetBlackout(false);
            break;

        case COMMAND_RESET_CROWNS:
            pendingActivation.active = false;
            pendingSync.active = false;
            pendingStrobe.active = false;
            effectsSetBlackout(false);
            if (isTotemNode()) {
                crownActivated = true;
                applyTotemIdle();
            } else {
                crownActivated = false;
                effectsSetActive(false);
            }
            break;

        case COMMAND_HEARTBEAT:
        default:
            if (crownActivated) {
                if (pendingActivation.active || pendingStrobe.active || pendingSync.active) {
                    effectsNoteSignal();
                } else {
                    effectsApplyMessage(incoming);
                }
            }
            break;
    }

    if (incoming.hopCount < MAX_HOPS) {
        incoming.hopCount++;
        scheduleRelay(incoming);
    }
}

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    WiFi.macAddress(localMac);

    effectsInit();
    if (isTotemNode()) {
        effectsSetSignalWarningEnabled(false);
        crownActivated = true;
        applyTotemIdle();
    } else {
        effectsSetSignalWarningEnabled(true);
        crownActivated = false;
        effectsStartPowerCheck();
    }

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
    processActivation();
    processGroupStrobe();
    processEffectSync();

    if (now - lastEffectUpdate >= 20) {
        lastEffectUpdate = now;
        effectsUpdate(now);
    }
}
