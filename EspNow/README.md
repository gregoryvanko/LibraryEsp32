# EspNow
Cette librairie permet d'envoyer un message de type String via le protocole ESP-NOW.
# Exemple de fichier main
```cpp
#include "espnow_comm.h"

void setup() {
  // setup serial
  Serial.begin(CONFIG_BAUDRATE);
  delay(1000);

  // Setup start
  Serial.println("\nsetup: start");

  // ESP-NOW
  if (!espnowComm.begin(RECEIVER_MAC, false)) {
      Serial.println("FATAL : Initialisation ESP-NOW échouée. Redémarrage...");
      delay(3000);
      ESP.restart();
  }

  // Setup done
  Serial.println("setup: done");
}

// ---loop-------------------------------------------------
void loop() {
  // Sérialisation data
  String dataSerialized = "Test";

  // Send ESP-NOW
  espnowComm.sendMessage(dataSerialized);
}
```
