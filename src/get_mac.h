#include <Arduino.h>
#include <WiFi.h>
#include <FastLED.h>

void setup() {
    Serial.begin(115200);
    
    // Attend que le port série soit vraiment prêt (indispensable sur C3)
    while (!Serial) {
        delay(10);
    }
    
    delay(1000); 
    Serial.println("\n--- CONNEXION REUSSIE ---");
    Serial.print("ADRESSE MAC : ");
    Serial.println(WiFi.macAddress());
    Serial.println("-------------------------");
}

void loop() {
    Serial.print("Coucou Caroline ! Mon adresse MAC est : ");
    Serial.println(WiFi.macAddress());
    delay(2000); // Elle le dira toutes les 2 secondes
}