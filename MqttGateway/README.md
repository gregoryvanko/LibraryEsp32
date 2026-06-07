# MqttGateway
Librairie pour se connecter, envoyer et recevoir un message sur un gateway MQTT
## Exemple de fichier main
```cpp
#include <WiFi.h>
#include "WifiHelper.h"
#include "MqttGateway.h"

static const char* CONFIG_MQTT_BROKER   = "192.168.40.40";  // IP ou hostname du broker
static const uint16_t CONFIG_MQTT_PORT  = 1883;
static const char* CONFIG_MQTT_CLIENT   = "ESP32-Hydro1";
static const char* CONFIG_MQTT_USER     = "xxx";
static const char* CONFIG_MQTT_PASS     = "xxx";

static const char* CONFIG_TOPIC_waterTemp    = "hydro1/waterTemp";
static const char* CONFIG_TOPIC_CMD          = "hydro1/cmd/#";  // wildcard
static const char* CONFIG_TOPIC_STATUS       = "hydro1/status";

constexpr uint16_t CONFIG_BAUDRATE = 9600;
constexpr uint32_t CONFIG_MESURE_INTERVAL = 5000;

// ── Objets globaux ────────────────────────────────────────────────────────────
WiFiClient  wifiClient;
MqttGateway mqtt(wifiClient, CONFIG_MQTT_BROKER, CONFIG_MQTT_PORT, CONFIG_MQTT_CLIENT);

uint32_t lastMesure = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Callback réception MQTT
// ─────────────────────────────────────────────────────────────────────────────
void onMqttMessage(const String& topic, const String& payload) {
    Serial.printf("[APP] Message reçu — topic: %s | payload: %s\n",
                  topic.c_str(), payload.c_str());
 
    // Exemple de dispatch par topic
    if (topic == "hydro/cmd/pump") {
      bool on = (payload == "ON" || payload == "1");
      Serial.printf("[APP] Commande pompe : %s\n", on ? "MARCHE" : "ARRÊT");
      // digitalWrite(PIN_PUMP, on ? HIGH : LOW);
    }
    else if (topic == "hydro/cmd/light") {
      // …
    } else {
      Serial.printf("[APP] topic not found");
    }
}

// ---setup------------------------------------------------
void setup() {
  // setup serial
  Serial.begin(CONFIG_BAUDRATE);
  delay(1000);

  // Setup start
  Serial.println("\nsetup: start");

  // Wifi connection to routeur
  connectToWiFi(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);

  // MQTT connection
  mqtt.setCredentials(CONFIG_MQTT_USER, CONFIG_MQTT_PASS);
  mqtt.setLastWill(CONFIG_TOPIC_STATUS, "offline", /*retain=*/true, /*qos=*/1);
  mqtt.setMessageCallback(onMqttMessage);

  if (!mqtt.connect(5)) {
      Serial.println("[APP] Impossible de se connecter au broker. Redémarrage…");
      ESP.restart();
  }

  mqtt.subscribe(CONFIG_TOPIC_CMD, 1);
  mqtt.publish(CONFIG_TOPIC_STATUS, "online", /*retain=*/true);

  // Setup done
  Serial.println("setup: done");
}

// ---loop-------------------------------------------------
void loop() {
  // Reconnexion Wi-Fi si nécessaire (non bloquant)
  handleWiFiReconnect();

  // Maintien connexion mqtt + réception messages
  mqtt.loop();

  // Initialisation de la mesure du temps
  uint32_t now = millis();
  // Si l'intervale de temps s'est écoulé
  if (now - lastMesure >= CONFIG_MESURE_INTERVAL){
    // Actualisation de la mesure du temps
    lastMesure = now;
    float waterTemp = 20.22;
    // send MQTT
    mqtt.publish(CONFIG_TOPIC_waterTemp, String(waterTemp, 2));
  }

  
}
```
