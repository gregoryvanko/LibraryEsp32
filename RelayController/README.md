# RelayController
Cette librairie permet de piloter un relais

## Exemple de fichier main
```cpp
#include "RelayController.h"

constexpr uint8_t CONFIG_PIN_RELAY1 = 32;

constexpr uint16_t CONFIG_BAUDRATE = 9600;
constexpr uint32_t CONFIG_MESURE_INTERVAL = 5000;

RelayController relay1(CONFIG_PIN_RELAY1, true);

// Deniere mesure (ms)
uint32_t lastMesure = 0;

// ---setup------------------------------------------------
void setup() {
  // setup serial
  Serial.begin(CONFIG_BAUDRATE);
  delay(1000);

  // Setup start
  Serial.println("\n[Setup] start");

  // Start relay
  relay1.begin();

  // Setup done
  Serial.println("[Setup] done");

  // Test Relay
  relay1.turnOn();
  delay(1000);
  relay1.turnOff();
}

// ---loop-------------------------------------------------
void loop() {
  // Initialisation de la mesure du temps
  uint32_t now = millis();

  if (now - lastMesure >= CONFIG_MESURE_INTERVAL){
    relay1.toggle();
  }
}
```
