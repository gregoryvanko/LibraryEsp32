# SensorTemperature
Cette librairie permet de mesurer la temperature d'une sonde DS18B20

## Exemple de fichier main
```cpp
#include "SensorTemperature.h"

constexpr uint8_t CONFIG_PIN_DS18B20 = 4;

constexpr uint16_t CONFIG_BAUDRATE = 9600;
constexpr uint32_t CONFIG_MESURE_INTERVAL = 5000;

SensorTemperature SensorTemperature1(CONFIG_PIN_DS18B20);

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
  SensorTemperature1.begin();

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
    float waterTemp = SensorTemperature1.read();
    // Print mesures
    Serial.printf("[DATA] Temp=%.2f°C \n",waterTemp);
  }
}
```
