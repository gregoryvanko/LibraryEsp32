#include "SensorTemperature.h"

SensorTemperature::SensorTemperature(uint8_t pin)
  : _oneWire(pin), _sensor(&_oneWire){}

void SensorTemperature::begin(){
  _sensor.begin();

  uint8_t count = _sensor.getDeviceCount();
  if (count == 0) {
      Serial.println("[DS18B20] ⚠ Aucun capteur détecté !");
  } else {
      Serial.println("[DS18B20] Capteur initialisé.");
  }
}

float SensorTemperature::read() {
    _sensor.requestTemperatures();          // conversion ~750ms
    float temp = _sensor.getTempCByIndex(0);

    if (temp == DEVICE_DISCONNECTED_C) {
        Serial.println("[DS18B20] ⚠ Erreur de lecture !");
    }
    return temp;
}