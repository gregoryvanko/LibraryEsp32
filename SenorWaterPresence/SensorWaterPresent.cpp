#include "SensorWaterPresent.h"

SensorWaterPresent::SensorWaterPresent(uint8_t pin, bool pullup)
  : _pin(pin), _pullup(pullup){}

void SensorWaterPresent::begin(){
  if(_pullup){
    pinMode(_pin, INPUT_PULLUP);
  } else {
    pinMode(_pin, INPUT);
  }
}

bool SensorWaterPresent::read(){
  {
      return (digitalRead(_pin) == HIGH) ? 1 : 0;
  }
}