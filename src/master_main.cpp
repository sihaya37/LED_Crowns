#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ctype.h>
#include "config.h"

#define RST_PIN 22
#define SS_PIN  21

MFRC522 rfid(SS_PIN, RST_PIN);

struct_message myData;
uint32_t sessionId = 0;
uint32_t msgCounter = 0;

struct PendingSend {
    struct_message message;
    uint8_t remaining;
    unsigned long nextSendAt;
    bool active;
};

PendingSend pendingSend = {};

struct PendingRemoteCommand {
    struct_remote_command command;
    bool active;
};

PendingRemoteCommand pendingRemoteCommand = {};

struct CrownActivation {
    const char* crownName;
    uint8_t targetMac[6];
    uint8_t tagUid[10];
    uint8_t tagLength;
    uint8_t effectId;
    const char* name;
    uint8_t intensity;
    uint8_t speed;
    uint32_t primaryColor;
    uint32_t secondaryColor;
};

const CrownActivation crownActivations[] = {
    {"A", {0x3C, 0xDC, 0x75, 0x33, 0x78, 0x74}, {0x04, 0xA8, 0xC7, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_PORTAL, "PORTAL blue violet", 205, 92, 0x5F7CFF, 0xD100FF},
    {"D", {0x3C, 0xDC, 0x75, 0x32, 0xF6, 0x24}, {0x04, 0x92, 0xC1, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_THEATER_CHASE, "THEATER CHASE violet", 185, 105, 0xD92CFF, 0x09001F},
    {"E", {0x3C, 0xDC, 0x75, 0x33, 0x64, 0xDC}, {0x04, 0x7E, 0xA3, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_QUEEN_AURA, "QUEEN AURA pink violet", 190, 28, 0xFF6AD5, 0x5E2CFF},
    {"F", {0x3C, 0xDC, 0x75, 0x33, 0x78, 0x50}, {0x04, 0x7D, 0xA3, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_SPARKLE, "SPARKLE white gold", 190, 70, 0xFFF2B0, 0x000000},
    {"H", {0xAC, 0xEB, 0xE6, 0x6E, 0x87, 0x94}, {}, 0, EFFECT_WAVE, "WAVE queen magenta", 180, 35, 0xFF20A8, 0x080010},
    {"I", {0x3C, 0xDC, 0x75, 0x33, 0x35, 0xA0}, {0x04, 0xD2, 0xCD, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_GLITTER_RAIN, "GLITTER RAIN ice pink", 185, 80, 0xFFD6F6, 0x173CFF},
    {"J", {0x08, 0x92, 0x72, 0x25, 0x47, 0x80}, {}, 0, EFFECT_COMET_TWINS, "COMET TWINS magenta cyan", 210, 48, 0xFF1FBF, 0x16D7FF},
    {"K", {0x08, 0x92, 0x72, 0x25, 0x49, 0x68}, {0x04, 0x66, 0x9C, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_STORM, "STORM electric blue", 220, 86, 0x76E8FF, 0x230060},
    {"L", {0x3C, 0xDC, 0x75, 0x33, 0x73, 0x44}, {0x04, 0x26, 0xE8, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_VOGUE_POSE, "VOGUE POSE sharp magenta", 215, 112, 0xFF1FBF, 0x1212A8},
    {"M", {0x3C, 0xDC, 0x75, 0x31, 0x7E, 0x08}, {0x04, 0xE6, 0xD4, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_RIPPLE, "RIPPLE blue pink", 205, 76, 0x1E90FF, 0xFF4FD8},
    {"N", {0x08, 0x92, 0x72, 0x25, 0x62, 0x98}, {}, 0, EFFECT_PORTAL, "PORTAL blue violet", 205, 92, 0x5F7CFF, 0xD100FF},
    {"O", {0x08, 0x92, 0x72, 0x23, 0xCE, 0xA8}, {0x04, 0x5F, 0x96, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_BREATH, "BREATH deep red", 180, 18, 0xB00020, 0x000000},
    {"Q", {0x08, 0x92, 0x72, 0x23, 0xCF, 0xF8}, {0x04, 0x91, 0xA9, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_WAVE, "WAVE queen magenta", 180, 35, 0xFF20A8, 0x080010},
    {"R", {0x08, 0x92, 0x72, 0x23, 0xD7, 0xA0}, {0x04, 0x75, 0xBA, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_COLOR_CHASE, "COLOR CHASE royal blue", 190, 92, 0x2274FF, 0x7D1AFF},
    {"S", {0x3C, 0xDC, 0x75, 0x33, 0x80, 0x08}, {}, 0, EFFECT_PRISM, "PRISM saturated rainbow", 170, 32, 0xFF2BD6, 0x1C4DFF},
    {"T", {0x08, 0x92, 0x72, 0x25, 0x49, 0x1C}, {0x04, 0x19, 0xE2, 0x73, 0xCE, 0x2A, 0x81}, 7, EFFECT_CONSTELLATION, "CONSTELLATION violet stars", 210, 38, 0xF8E8FF, 0x25004D}
};

const uint8_t CROWN_ACTIVATION_COUNT = sizeof(crownActivations) / sizeof(crownActivations[0]);

struct PendingHeartbeat {
    struct_message message;
    const char* label;
    unsigned long activateAt;
    bool active;
};

PendingHeartbeat pendingHeartbeat = {};
struct_message activeHeartbeatMessage = {};
bool hasActiveHeartbeat = false;
const char* activeHeartbeatLabel = "none";
struct_message savedBlackoutHeartbeatMessage = {};
bool savedBlackoutHasHeartbeat = false;
const char* savedBlackoutHeartbeatLabel = "none";
bool blackoutActive = false;

uint8_t lastUid[10] = {};
uint8_t lastUidLength = 0;
unsigned long lastScanAt = 0;

uint32_t lastRemoteSessionId = 0;
uint32_t lastRemoteMsgId = 0;
bool hasRemoteSession = false;

const char FINAL_EFFECT_NAME[] = "FINAL rave";

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

void prepareMessage(const CrownActivation& effect, uint8_t command) {
    msgCounter++;

    myData.protocolVersion = PROTOCOL_VERSION;
    myData.command = command;
    myData.effectId = effect.effectId;
    myData.hopCount = 0;
    myData.intensity = effect.intensity;
    myData.speed = effect.speed;
    myData.flags = 0;
    myData.sessionId = sessionId;
    myData.msgId = msgCounter;
    myData.primaryColor = effect.primaryColor;
    myData.secondaryColor = effect.secondaryColor;
    memcpy(myData.targetMac, effect.targetMac, sizeof(myData.targetMac));
}

void prepareGlobalMessage(
    uint8_t command,
    uint8_t effectId,
    uint8_t intensity,
    uint8_t speed,
    uint32_t primaryColor,
    uint32_t secondaryColor
) {
    msgCounter++;

    myData.protocolVersion = PROTOCOL_VERSION;
    myData.command = command;
    myData.effectId = effectId;
    myData.hopCount = 0;
    myData.intensity = intensity;
    myData.speed = speed;
    myData.flags = 0;
    myData.sessionId = sessionId;
    myData.msgId = msgCounter;
    myData.primaryColor = primaryColor;
    myData.secondaryColor = secondaryColor;
    memset(myData.targetMac, 0, sizeof(myData.targetMac));
}

void prepareHeartbeatFromTemplate(const struct_message& effectTemplate) {
    msgCounter++;

    myData = effectTemplate;
    myData.command = COMMAND_HEARTBEAT;
    myData.hopCount = 0;
    myData.sessionId = sessionId;
    myData.msgId = msgCounter;
}

void queueHeartbeatTemplate(const struct_message& message, const char* label, unsigned long delayMs) {
    pendingHeartbeat.message = message;
    pendingHeartbeat.message.command = COMMAND_HEARTBEAT;
    pendingHeartbeat.label = label;
    pendingHeartbeat.activateAt = millis() + delayMs;
    pendingHeartbeat.active = true;
}

void sendGlobalEffect(
    uint8_t effectId,
    const char* label,
    uint8_t intensity,
    uint8_t speed,
    uint32_t primaryColor,
    uint32_t secondaryColor
) {
    prepareGlobalMessage(COMMAND_GLOBAL_EFFECT, effectId, intensity, speed, primaryColor, secondaryColor);
    scheduleBroadcast(myData);
    queueHeartbeatTemplate(myData, label, 0);
    blackoutActive = false;
    Serial.printf("[REMOTE] Effet global -> %s id=%u\n", label, effectId);
}

bool uidEquals(const uint8_t* left, uint8_t leftLength, const uint8_t* right, uint8_t rightLength) {
    return leftLength == rightLength && memcmp(left, right, leftLength) == 0;
}

void printUid(const uint8_t* uid, uint8_t length) {
    for (uint8_t i = 0; i < length; i++) {
        if (uid[i] < 0x10) {
            Serial.print('0');
        }
        Serial.print(uid[i], HEX);
        if (i + 1 < length) {
            Serial.print(':');
        }
    }
}

const CrownActivation* findActivationByUid(const uint8_t* uid, uint8_t length) {
    for (uint8_t i = 0; i < CROWN_ACTIVATION_COUNT; i++) {
        const CrownActivation& activation = crownActivations[i];
        if (activation.tagLength == 0) {
            continue;
        }
        if (uidEquals(uid, length, activation.tagUid, activation.tagLength)) {
            return &activation;
        }
    }

    return nullptr;
}

const CrownActivation* findActivationByCrown(char crownName) {
    char wanted = toupper((unsigned char)crownName);

    for (uint8_t i = 0; i < CROWN_ACTIVATION_COUNT; i++) {
        const CrownActivation& activation = crownActivations[i];
        if (activation.crownName[0] == wanted && activation.tagLength > 0) {
            return &activation;
        }
    }

    return nullptr;
}

bool isRepeatedScan(const uint8_t* uid, uint8_t length, unsigned long now) {
    return now - lastScanAt < 1500 && uidEquals(uid, length, lastUid, lastUidLength);
}

void rememberScan(const uint8_t* uid, uint8_t length, unsigned long now) {
    memcpy(lastUid, uid, length);
    lastUidLength = length;
    lastScanAt = now;
}

void processRfid() {
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        return;
    }

    unsigned long now = millis();
    uint8_t uidLength = rfid.uid.size;
    uint8_t uid[10] = {};
    memcpy(uid, rfid.uid.uidByte, uidLength);

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    if (isRepeatedScan(uid, uidLength, now)) {
        return;
    }
    rememberScan(uid, uidLength, now);

    const CrownActivation* activation = findActivationByUid(uid, uidLength);
    if (activation == nullptr) {
        Serial.print("Tag inconnu. UID=");
        printUid(uid, uidLength);
        Serial.println("  -> ajoute cet UID dans crownActivations[].");
        return;
    }

    prepareMessage(*activation, COMMAND_ACTIVATE_CROWN);
    scheduleBroadcast(myData);
    queueHeartbeatTemplate(myData, activation->name, SHOW_HEARTBEAT_SYNC_DELAY);
    blackoutActive = false;

    Serial.printf(
        "Activation couronne %s: effect=%s id=%u. Sync des couronnes deja actives dans %u ms.\n",
        activation->crownName,
        activation->name,
        activation->effectId,
        SHOW_HEARTBEAT_SYNC_DELAY);
}

void processPendingHeartbeat() {
    if (!pendingHeartbeat.active) {
        return;
    }

    unsigned long now = millis();
    if ((long)(now - pendingHeartbeat.activateAt) < 0) {
        return;
    }

    activeHeartbeatMessage = pendingHeartbeat.message;
    activeHeartbeatLabel = pendingHeartbeat.label;
    hasActiveHeartbeat = true;
    pendingHeartbeat.active = false;
    Serial.printf("Heartbeat synchronise sur: %s\n", activeHeartbeatLabel);
}

bool isDuplicateRemoteCommand(const struct_remote_command& command) {
    if (!hasRemoteSession || command.sessionId != lastRemoteSessionId) {
        hasRemoteSession = true;
        lastRemoteSessionId = command.sessionId;
        lastRemoteMsgId = 0;
    }

    if (command.msgId <= lastRemoteMsgId) {
        return true;
    }

    lastRemoteMsgId = command.msgId;
    return false;
}

void activateCrownFromRemote(char crownName) {
    const CrownActivation* activation = findActivationByCrown(crownName);
    if (activation == nullptr) {
        Serial.printf("[REMOTE] Couronne %c inconnue ou sans tag associe.\n", toupper((unsigned char)crownName));
        return;
    }

    prepareMessage(*activation, COMMAND_ACTIVATE_CROWN);
    scheduleBroadcast(myData);
    queueHeartbeatTemplate(myData, activation->name, SHOW_HEARTBEAT_SYNC_DELAY);
    blackoutActive = false;

    Serial.printf("[REMOTE] Activation couronne %s -> %s\n", activation->crownName, activation->name);
}

void processRemoteCommand(const struct_remote_command& command) {
    if (command.protocolVersion != PROTOCOL_VERSION || isDuplicateRemoteCommand(command)) {
        return;
    }

    switch (command.remoteCommand) {
        case REMOTE_BLACKOUT:
            if (blackoutActive) {
                prepareGlobalMessage(COMMAND_RESTORE_FROM_BLACKOUT, EFFECT_OFF, 0, 0, 0x000000, 0x000000);
                scheduleBroadcast(myData);
                hasActiveHeartbeat = savedBlackoutHasHeartbeat;
                activeHeartbeatMessage = savedBlackoutHeartbeatMessage;
                activeHeartbeatLabel = savedBlackoutHeartbeatLabel;
                blackoutActive = false;
                Serial.println("[REMOTE] Restore apres blackout");
            } else {
                savedBlackoutHasHeartbeat = hasActiveHeartbeat;
                savedBlackoutHeartbeatMessage = activeHeartbeatMessage;
                savedBlackoutHeartbeatLabel = activeHeartbeatLabel;
                prepareGlobalMessage(COMMAND_BLACKOUT, EFFECT_OFF, 0, 0, 0x000000, 0x000000);
                scheduleBroadcast(myData);
                hasActiveHeartbeat = false;
                pendingHeartbeat.active = false;
                blackoutActive = true;
                Serial.println("[REMOTE] Blackout general");
            }
            break;

        case REMOTE_RESET:
            prepareGlobalMessage(COMMAND_RESET_CROWNS, EFFECT_OFF, 0, 0, 0x000000, 0x000000);
            scheduleBroadcast(myData);
            hasActiveHeartbeat = false;
            pendingHeartbeat.active = false;
            blackoutActive = false;
            Serial.println("[REMOTE] Reset couronnes");
            break;

        case REMOTE_TEST:
            prepareGlobalMessage(COMMAND_TEST_NETWORK, EFFECT_DEBUG_HOPS, 255, 0, 0x000000, 0x000000);
            scheduleBroadcast(myData);
            queueHeartbeatTemplate(myData, "TEST network debug hops", 0);
            blackoutActive = false;
            Serial.println("[REMOTE] Test reseau / debug hops");
            break;

        case REMOTE_FINAL:
            sendGlobalEffect(EFFECT_FINAL_RAVE, FINAL_EFFECT_NAME, 240, 138, 0xFF2BD6, 0x1C4DFF);
            break;

        case REMOTE_PARTY:
            sendGlobalEffect(EFFECT_PARTY_PULSE, "PARTY pulse", 220, 132, 0xFF2BD6, 0x20D8FF);
            break;

        case REMOTE_POMPON:
            sendGlobalEffect(EFFECT_POMPON_SPARKLE, "POMPON sparkle", 235, 115, 0xFFE078, 0xFF4FD8);
            break;

        case REMOTE_PUBLIC_WAVE:
            sendGlobalEffect(EFFECT_PUBLIC_WAVE, "PUBLIC wave", 230, 72, 0xFF24BE, 0x1EA8FF);
            break;

        case REMOTE_RAVE:
            sendGlobalEffect(EFFECT_FINAL_RAVE, "FINAL rave", 245, 150, 0xFF2BD6, 0x1C4DFF);
            break;

        case REMOTE_FREEZE:
            sendGlobalEffect(EFFECT_FINAL_FREEZE, "FINAL freeze", 245, 0, 0xFFFFFF, 0xFF23D2);
            break;

        case REMOTE_ACTIVATE_CROWN:
            activateCrownFromRemote(command.targetCrown);
            break;

        default:
            Serial.printf("[REMOTE] Commande inconnue: %u\n", command.remoteCommand);
            break;
    }
}

void processPendingRemoteCommand() {
    if (!pendingRemoteCommand.active) {
        return;
    }

    struct_remote_command command = pendingRemoteCommand.command;
    pendingRemoteCommand.active = false;
    processRemoteCommand(command);
}

void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(struct_remote_command)) {
        return;
    }

    memcpy(&pendingRemoteCommand.command, data, sizeof(pendingRemoteCommand.command));
    pendingRemoteCommand.active = true;
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

    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

    WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4));
    Serial.printf("Master RFID pret. Canal=%d, TX=%d, Session=%u\n", WIFI_CHANNEL, TX_POWER_LEVEL, sessionId);
    Serial.println("Scanne un tag. Si l'UID est inconnu, il sera affiche ici pour l'ajouter au mapping.");
    Serial.println("Canal regie ESP-NOW actif: blackout/test/final/reset/couronne.");
}

void loop() {
    static unsigned long lastSend = 0;
    unsigned long now = millis();

    processBroadcastQueue();
    processRfid();
    processPendingRemoteCommand();
    processPendingHeartbeat();

    if (!pendingSend.active && hasActiveHeartbeat && (lastSend == 0 || now - lastSend >= MASTER_HEARTBEAT_INTERVAL)) {
        lastSend = now;

        prepareHeartbeatFromTemplate(activeHeartbeatMessage);
        scheduleBroadcast(myData);

        Serial.printf(
            "Heartbeat msg=%u session=%u effect=%s id=%u intensity=%u speed=%u primary=#%06X secondary=#%06X\n",
            msgCounter,
            sessionId,
            activeHeartbeatLabel,
            activeHeartbeatMessage.effectId,
            activeHeartbeatMessage.intensity,
            activeHeartbeatMessage.speed,
            activeHeartbeatMessage.primaryColor,
            activeHeartbeatMessage.secondaryColor);
    }
}
