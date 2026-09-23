#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>
#include <functional>

// Gestion d'un bouton connecté sur une GPIO, avec anti-rebond (debounce).
// Utilisation : appeler begin() dans setup() puis update() à chaque tour de loop().
class Button {
public:
  using Callback = std::function<void()>;

  // pin        : numéro de la GPIO
  // usePullUp  : true  -> INPUT_PULLUP (résistance interne activée)
  //              false -> INPUT (résistance externe nécessaire)
  // debounceMs : durée de stabilité (ms) requise avant de valider un changement d'état
  Button(uint8_t pin, bool usePullUp = true, uint32_t debounceMs = 50);

  // Configure la GPIO. À appeler dans setup().
  void begin();

  // Lit la GPIO, applique le debounce et déclenche les callbacks.
  // À appeler le plus souvent possible dans loop().
  void update();

  // Fonction appelée quand le bouton passe à LOW / HIGH (après debounce)
  void onLow(Callback callback);
  void onHigh(Callback callback);

  // État stable actuel (après debounce)
  bool isLow() const;
  bool isHigh() const;

  uint8_t getPin() const;

private:
  uint8_t _pin;
  bool _usePullUp;
  uint32_t _debounceMs;

  int _stableState;         // dernier état validé (HIGH ou LOW)
  int _lastReading;         // dernière lecture brute
  uint32_t _lastChangeTime; // instant (ms) du dernier changement de lecture brute

  Callback _onLow;
  Callback _onHigh;
};

#endif
