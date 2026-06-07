# SensorSonar
Cette librairie permet de mesurer la présence d'eau via un capteur de CQRobot

## Exemple de fichier main
```cpp
#include "SensorWaterPresent.h"

constexpr uint8_t CONFIG_PIN_WaterPres = 35;

constexpr uint16_t CONFIG_BAUDRATE = 9600;
constexpr uint32_t CONFIG_MESURE_INTERVAL = 5000;

SensorWaterPresent SensorWaterPresent1(CONFIG_PIN_WaterPres, false);

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
  SensorWaterPresent1.begin();

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
    bool waterPresent = SensorWaterPresent1.read();
    // Print mesures
    Serial.printf("[DATA] Flux=%s \n",waterPresent ? "OUI" : "NON");
  }
}
```
