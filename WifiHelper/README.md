# Wifi Helper
ce fichier permet de simplifier la connection wifi d'un ESP32
## Exemple de fichier main
```cpp
#include "WifiHelper.h"

constexpr char* CONFIG_WIFI_SSID     = "xxx-IOT";
constexpr char* CONFIG_WIFI_PASSWORD = "xxx";

constexpr uint16_t CONFIG_BAUDRATE = 9600;
constexpr uint32_t CONFIG_MESURE_INTERVAL = 5000;

void setup() {
  // setup serial
  Serial.begin(CONFIG_BAUDRATE);
  delay(1000);

  // Setup start
  Serial.println("\nsetup: start");

  // Wifi connection to routeur
  connectToWiFi(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);

  // Setup done
  Serial.println("setup: done");
}

void loop() {
  // Reconnexion Wi-Fi si nécessaire (non bloquant)
  handleWiFiReconnect();
}
```
