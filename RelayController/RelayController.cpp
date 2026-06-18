#include "RelayController.h"

RelayController::RelayController(uint8_t pin, bool activeLow)
    : _pin(pin), _activeLow(activeLow), _state(false) {}

void RelayController::begin() {
    pinMode(_pin, OUTPUT);
    turnOff();
    Serial.println("[Relay] Capteur initialisé.");
}

void RelayController::turnOn() {
    _state = true;
    digitalWrite(_pin, _activeLow ? LOW : HIGH);
    Serial.println("[Relay] Capteur On");
}

void RelayController::turnOff() {
    _state = false;
    digitalWrite(_pin, _activeLow ? HIGH : LOW);
    Serial.println("[Relay] Capteur Off");
}

void RelayController::toggle() {
    _state ? turnOff() : turnOn();
    Serial.println("[Relay] Capteur Toggle");
}

void RelayController::pulse(uint32_t durationMs) {
    turnOn();
    delay(durationMs);
    turnOff();
    Serial.println("[Relay] Pulse");
}

bool RelayController::isOn() const {
    return _state;
}

String RelayController::getStateString() const {
    return _state ? "ON" : "OFF";
}