#pragma once

#include <Arduino.h>
 
class RelayController {
public:
    RelayController(uint8_t pin, bool activeLow = true);
 
    void begin();
    void turnOn();
    void turnOff();
    void toggle();
    void pulse(uint32_t durationMs);
 
    bool   isOn()           const;
    String getStateString() const;
 
private:
    uint8_t _pin;
    bool    _activeLow;
    bool    _state;
};