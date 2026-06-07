# SensorTDS
Cette librairie permet de mesurer la valeur EC d'un capteur TDS

## Exemple de fichier main
```cpp
#include "SensorTDS.h"

constexpr uint8_t CONFIG_PIN_TDS = 34;

constexpr uint16_t CONFIG_BAUDRATE = 9600;
constexpr uint32_t CONFIG_MESURE_INTERVAL = 5000;

SensorTDS SensorTDS1(CONFIG_PIN_TDS);

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
  SensorTDS1.begin();

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
    float waterTemp = 20;
    float waterEc = SensorTDS1.readEC(waterTemp);
    // Print mesures
    Serial.printf("[DATA] EC=%.2fmS/cm \n",waterEc);
  }
}
```
