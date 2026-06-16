#pragma once
#include <Arduino.h>

class SensorWaterPresent {
  public:
    explicit SensorWaterPresent(uint8_t pin, bool pullup = false);
    void begin();
    bool read();

  private:
    uint8_t _pin; 
    bool _pullup;
};