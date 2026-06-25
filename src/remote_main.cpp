#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ctype.h>
#include "config.h"

struct PendingSend {
    struct_remote_command command;
    uint8_t remaining;
    unsigned long nextSendAt;
    bool active;
};

PendingSend pendingSend = {};

uint32_t remoteSessionId = 0;
uint32_t remoteMsgCounter = 0;
String inputLine;

void printHelp() {
    Serial.println();
    Serial.println("Commandes regie:");
    Serial.println("  b / blackout  -> blackout general");
    Serial.println("  r / reset     -> reset couronnes, retour eteint");
    Serial.println("  t / test      -> test reseau / debug hops");
    Serial.println("  p / party     -> party pulse global");
    Serial.println("  s / pompon    -> sparkle pompons / paillettes");
    Serial.println("  w / wave      -> vague scene/public");
    Serial.println("  v / rave      -> final rave / prism boost");
    Serial.println("  f / final     -> final rave global");
    Serial.println("  x / freeze    -> pose finale blanc fixe");
    Serial.println("  cA, cD, cO... -> activation manuelle d'une couronne");
    Serial.println("  h / help      -> aide");
    Serial.println();
}

void scheduleRemoteSend(uint8_t remoteCommand, char targetCrown = 0) {
    remoteMsgCounter++;

    pendingSend.command.protocolVersion = PROTOCOL_VERSION;
    pendingSend.command.remoteCommand = remoteCommand;
    pendingSend.command.targetCrown = targetCrown;
    pendingSend.command.reserved = 0;
    pendingSend.command.sessionId = remoteSessionId;
    pendingSend.command.msgId = remoteMsgCounter;
    pendingSend.remaining = MSG_REDUNDANCY_COUNT;
    pendingSend.nextSendAt = millis();
    pendingSend.active = true;
}

void processSendQueue() {
    if (!pendingSend.active || pendingSend.remaining == 0) {
        pendingSend.active = false;
        return;
    }

    unsigned long now = millis();
    if ((long)(now - pendingSend.nextSendAt) < 0) {
        return;
    }

    WiFi.setTxPower((wifi_power_t)(TX_POWER_LEVEL * 4));
    esp_err_t result = esp_now_send(BROADCAST_ADDR, (uint8_t*)&pendingSend.command, sizeof(pendingSend.command));
    Serial.printf(
        "Envoi #%u cmd=%u cible=%c tentative=%u/%u -> %s\n",
        pendingSend.command.msgId,
        pendingSend.command.remoteCommand,
        pendingSend.command.targetCrown == 0 ? '-' : pendingSend.command.targetCrown,
        MSG_REDUNDANCY_COUNT - pendingSend.remaining + 1,
        MSG_REDUNDANCY_COUNT,
        result == ESP_OK ? "OK" : "ERREUR");

    pendingSend.remaining--;
    pendingSend.nextSendAt = now + MSG_REDUNDANCY_DELAY;

    if (pendingSend.remaining == 0) {
        pendingSend.active = false;
    }
}

void handleCommand(String command) {
    command.trim();
    command.toLowerCase();

    if (command.length() == 0) {
        return;
    }

    if (command == "h" || command == "help" || command == "?") {
        printHelp();
        return;
    }

    if (command == "b" || command == "blackout") {
        Serial.println("Commande: blackout general");
        scheduleRemoteSend(REMOTE_BLACKOUT);
        return;
    }

    if (command == "r" || command == "reset") {
        Serial.println("Commande: reset couronnes");
        scheduleRemoteSend(REMOTE_RESET);
        return;
    }

    if (command == "t" || command == "test") {
        Serial.println("Commande: test reseau");
        scheduleRemoteSend(REMOTE_TEST);
        return;
    }

    if (command == "f" || command == "final") {
        Serial.println("Commande: final rave global");
        scheduleRemoteSend(REMOTE_FINAL);
        return;
    }

    if (command == "p" || command == "party") {
        Serial.println("Commande: party pulse global");
        scheduleRemoteSend(REMOTE_PARTY);
        return;
    }

    if (command == "s" || command == "sparkle" || command == "pompon") {
        Serial.println("Commande: sparkle pompons / paillettes");
        scheduleRemoteSend(REMOTE_POMPON);
        return;
    }

    if (command == "w" || command == "wave" || command == "public") {
        Serial.println("Commande: vague scene/public");
        scheduleRemoteSend(REMOTE_PUBLIC_WAVE);
        return;
    }

    if (command == "v" || command == "rave") {
        Serial.println("Commande: final rave / prism boost");
        scheduleRemoteSend(REMOTE_RAVE);
        return;
    }

    if (command == "x" || command == "freeze") {
        Serial.println("Commande: pose finale blanc fixe");
        scheduleRemoteSend(REMOTE_FREEZE);
        return;
    }

    if (command.length() == 2 && command.charAt(0) == 'c' && isalpha((unsigned char)command.charAt(1))) {
        char crown = toupper((unsigned char)command.charAt(1));
        Serial.printf("Commande: activation manuelle couronne %c\n", crown);
        scheduleRemoteSend(REMOTE_ACTIVATE_CROWN, crown);
        return;
    }

    Serial.print("Commande inconnue: ");
    Serial.println(command);
    printHelp();
}

void readSerialCommands() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            handleCommand(inputLine);
            inputLine = "";
            continue;
        }

        if (inputLine.length() < 48) {
            inputLine += c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1200);

    remoteSessionId = esp_random();
    inputLine.reserve(48);

    Serial.println();
    Serial.println("--- REMOTE C3 REGIE START ---");
    Serial.printf("Protocol=%u Canal=%u TX=%u Session=%u\n", PROTOCOL_VERSION, WIFI_CHANNEL, TX_POWER_LEVEL, remoteSessionId);

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
    Serial.println("Remote pret. Tape h pour l'aide.");
    printHelp();
}

void loop() {
    readSerialCommands();
    processSendQueue();
}
