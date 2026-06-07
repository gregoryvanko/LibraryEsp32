# SensorSonar
Cette librairie permet de controler un capoteur de distance sonar de type JNS-SRO4T

## Exemple de fichier main
```cpp
#include "SensorSonar.h"

SensorSonar SensorSonar1(CONFIG_PIN_TRIG, CONFIG_PIN_ECHO);

// Deniere mesure (ms)
uint32_t lastMesure = 0;

// ---setup------------------------------------------------
void setup() {
  // setup serial
  Serial.begin(CONFIG_BAUDRATE);
  delay(1000);

  // Setup start
  Serial.println("\nsetup: start");

  // Start sensor
  SensorSonar1.begin();

  // Setup done
  Serial.println("setup: done");
}

// ---loop-------------------------------------------------
void loop() {
  // Initialisation de la mesure du temps
  uint32_t now = millis();

  if (now - lastMesure >= CONFIG_MESURE_INTERVAL){
    // Actualisation de la mesure du temps
    lastMesure = now;
    // Mesure
    float waterLevel = SensorSonar1.measureMedianCm(9);
    // Print mesures
    Serial.printf("[DATA] Niveau=%.1fcm \n",waterLevel);
  }
}
```
