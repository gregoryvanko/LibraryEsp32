# CanManager
Cette librairie permet de controler un module CAN via un module SN65HVD230

## Exemple de fichier main qui envoie des messsages CAN
```cpp
#include "CANManager.h"

constexpr uint8_t CONFIG_PIN_CAN_TX = 17;
constexpr uint8_t CONFIG_PIN_CAN_RX = 16;

constexpr uint32_t CONFIG_CAN_BAUDRATE        = 500000;
constexpr uint32_t CONFIG_CAN_ID_waterTemp    = 0x101;
constexpr uint32_t CONFIG_CAN_ID_waterPresent = 0x102;

constexpr uint16_t CONFIG_BAUDRATE = 9600;
constexpr uint32_t CONFIG_MESURE_INTERVAL = 5000;

// Crée une instance du gestionnaire CAN
CANManager can;

// Deniere mesure (ms)
uint32_t lastMesure = 0;

// ---setup------------------------------------------------
void setup() {
  // setup serial
  Serial.begin(CONFIG_BAUDRATE);
  delay(1000);

  // Setup start
  Serial.println("\n[Setup] start");

  // Initialisation du bus CAN
  if (!can.begin(CONFIG_PIN_CAN_RX, CONFIG_PIN_CAN_TX, CONFIG_CAN_BAUDRATE)) {
      Serial.println("[Setup] ERREUR: Impossible d'initialiser le CAN!");
      while (1) {
          delay(100);
      }
  }

  // Setup done
  Serial.println("[Setup] done");
}

// ---loop-------------------------------------------------
void loop() {
  // Initialisation de la mesure du temps
  uint32_t now = millis();

  if (now - lastMesure >= CONFIG_MESURE_INTERVAL){
    // Actualisation de la mesure du temps
    lastMesure = now;

    // Send Can
    can.sendFloat(CONFIG_CAN_ID_waterTemp, waterTemp);
    delay(10);
    can.sendBool(CONFIG_CAN_ID_waterPresent, waterPresent);
    delay(10);
  }
}
```

## Exemple de fichier main qui recoit des messsages CAN
```cpp
#include "CANManager.h"

constexpr uint8_t CONFIG_PIN_CAN_TX = 17;
constexpr uint8_t CONFIG_PIN_CAN_RX = 16;

constexpr uint32_t CONFIG_CAN_BAUDRATE        = 500000;
constexpr uint32_t CONFIG_CAN_ID_waterTemp    = 0x101;
constexpr uint32_t CONFIG_CAN_ID_waterPresent = 0x102;

constexpr uint16_t CONFIG_BAUDRATE = 9600;

// Crée une instance du gestionnaire CAN
CANManager can;

// ---------------------------------------------------------------------------
// Traitement des message CAN reçues
// ---------------------------------------------------------------------------
void traiterCanMsg(const CANManager::CANMessage &rxMessage){
  switch (rxMessage.id) {

    case CONFIG_CAN_ID_waterPresent: {
      bool value = CANManager::decodeBool(rxMessage);
      Serial.printf("Flux=%s\n", value ? "true" : "false");
      break;
    }

    case CONFIG_CAN_ID_waterTemp: {
      float value = CANManager::decodeFloat(rxMessage);
      Serial.printf("Temp=%.2f°C\n", value);
      break;
    }

    default:
      Serial.printf("ID CAN inconnu : 0x%03lX\n", (unsigned long)rxMessage.id);
      break;
  }
}

// ---setup------------------------------------------------
void setup() {
  // setup serial
  Serial.begin(CONFIG_BAUDRATE);
  delay(1000);

  // Setup start
  Serial.println("\n[Setup] start");

  // Initialisation du bus CAN
  if (!can.begin(CONFIG_PIN_CAN_RX, CONFIG_PIN_CAN_TX, CONFIG_CAN_BAUDRATE)) {
      Serial.println("[Setup] ERREUR: Impossible d'initialiser le CAN!");
      while (1) {
          delay(100);
      }
  }

  // Setup done
  Serial.println("[Setup] done");
}

// ---loop-------------------------------------------------
void loop() {
  // Analyse des messages CAN
  CANManager::CANMessage rxMessage;
  if (can.receive(rxMessage)) {
    traiterCanMsg(rxMessage);
  }
}
```
