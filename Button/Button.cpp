#include "Button.h"

Button::Button(uint8_t pin, bool usePullUp, uint32_t debounceMs)
    : _pin(pin),
      _usePullUp(usePullUp),
      _debounceMs(debounceMs),
      _stableState(HIGH),
      _lastReading(HIGH),
      _lastChangeTime(0) {}

void Button::begin() {
  pinMode(_pin, _usePullUp ? INPUT_PULLUP : INPUT);

  // On part de l'état réel de la GPIO pour ne pas déclencher de callback au démarrage
  _stableState = digitalRead(_pin);
  _lastReading = _stableState;
  _lastChangeTime = millis();
}

void Button::update() {
  int reading = digitalRead(_pin);
  uint32_t now = millis();

  // La lecture a changé : on (re)démarre le compteur de debounce
  if (reading != _lastReading) {
    _lastReading = reading;
    _lastChangeTime = now;
  }

  // La lecture est stable depuis assez longtemps et diffère de l'état validé
  if (reading != _stableState && (now - _lastChangeTime) >= _debounceMs) {
    _stableState = reading;

    if (_stableState == LOW) {
      if (_onLow) _onLow();
    } else {
      if (_onHigh) _onHigh();
    }
  }
}

void Button::onLow(Callback callback) { _onLow = callback; }

void Button::onHigh(Callback callback) { _onHigh = callback; }

bool Button::isLow() const { return _stableState == LOW; }

bool Button::isHigh() const { return _stableState == HIGH; }

uint8_t Button::getPin() const { return _pin; }
