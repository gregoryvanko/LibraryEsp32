#pragma once
#include <Arduino.h>
#include <OneWire.h> // V2.3.8
#include <DallasTemperature.h> // V4.0.6

class SensorTemperature {
  public:
    explicit SensorTemperature(uint8_t pin);
    void begin();
    float read();

  private:
    OneWire _oneWire;
    DallasTemperature _sensor;
};