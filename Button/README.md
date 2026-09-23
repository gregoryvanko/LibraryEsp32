# Button

Librairie de gestion d'un bouton sur ESP32 (Arduino / PlatformIO), avec
anti-rebond (debounce) intégré.

Elle lit une GPIO, filtre les rebonds mécaniques du bouton et déclenche des
fonctions de rappel (callbacks) uniquement quand l'état est stable — sans
bloquer `loop()` (pas de `delay()`).

## Fonctionnement

1. **Lecture et anti-rebond :** à chaque appel de `update()`, la GPIO est
   lue. Si la lecture change, un compteur redémarre ; l'état n'est validé
   comme « stable » (et les callbacks déclenchés) que si la lecture reste
   identique pendant la durée de debounce configurée (50 ms par défaut).
2. **Callbacks :** `onLow()` est appelé quand l'état stable passe à `LOW`,
   `onHigh()` quand il passe à `HIGH`. Un seul appel par changement d'état,
   pas à chaque `update()`.
3. **État initial :** au `begin()`, l'état réel de la GPIO est lu et pris
   comme référence, pour ne déclencher aucun callback au démarrage.
4. **Pull-up :** par défaut, la résistance de pull-up interne de l'ESP32 est
   activée (`INPUT_PULLUP`), adaptée à un bouton câblé entre la GPIO et la
   masse (GND) — comme le bouton BOOT de la carte. `LOW` correspond alors à
   « appuyé ».

## Installation

Placer les fichiers `Button.h` et `Button.cpp` dans `lib/Button/` du projet
PlatformIO. Aucune dépendance externe (uniquement le framework Arduino).

## Utilisation minimale

```cpp
#include <Arduino.h>
#include "Button.h"

Button bouton(4);   // GPIO 4, pull-up interne activée, debounce 50 ms

void setup() {
  Serial.begin(115200);
  bouton.begin();

  bouton.onLow([]()  { Serial.println("Bouton appuyé"); });
  bouton.onHigh([]() { Serial.println("Bouton relâché"); });
}

void loop() {
  bouton.update();   // à appeler le plus souvent possible, sans délai bloquant
}
```

## Plusieurs boutons

Chaque bouton est une instance indépendante ; il suffit d'appeler `begin()`
et `update()` pour chacune (comme dans [main.cpp](../../src/main.cpp)) :

```cpp
Button bouton1(13, false, 50);  // GPIO 13, sans pull-up interne (résistance externe requise)
Button bouton2(14, false, 50);  // GPIO 14, sans pull-up interne

void ActionOnButtonChange(uint8_t pin, bool value) {
  Serial.printf("pin %u : %d\n", pin, value);
}

void setup() {
  Serial.begin(115200);

  bouton1.begin();
  bouton2.begin();

  bouton1.onLow([]()  { ActionOnButtonChange(1, 0); });
  bouton1.onHigh([]() { ActionOnButtonChange(1, 1); });

  bouton2.onLow([]()  { ActionOnButtonChange(2, 0); });
  bouton2.onHigh([]() { ActionOnButtonChange(2, 1); });
}

void loop() {
  bouton1.update();
  bouton2.update();
}
```

## Exemple : appui long

`Button` ne détecte que les changements d'état (appuyé / relâché), pas la
durée d'appui. Pour détecter un appui long (ex. 2 secondes), mesurer le
temps entre `onLow()` (début de l'appui) et `onHigh()` (relâchement), ou
interroger `isLow()` dans `loop()` :

```cpp
Button bouton(0);   // bouton BOOT
uint32_t pressStart = 0;
bool longPressHandled = false;

void setup() {
  bouton.begin();
  bouton.onLow([]()  { pressStart = millis(); longPressHandled = false; });
  bouton.onHigh([]() { longPressHandled = false; });
}

void loop() {
  bouton.update();

  if (bouton.isLow() && !longPressHandled && millis() - pressStart >= 2000) {
    longPressHandled = true;
    Serial.println("Appui long detecte !");
  }
}
```

## Constructeur

```cpp
Button(uint8_t pin, bool usePullUp = true, uint32_t debounceMs = 50);
```

| Paramètre    | Description                                                                 | Défaut |
|--------------|-------------------------------------------------------------------------------|--------|
| `pin`        | Numéro de la GPIO du bouton                                                  | —      |
| `usePullUp`  | `true` : résistance de pull-up interne (`INPUT_PULLUP`) ; `false` : `INPUT`, résistance externe nécessaire | `true` |
| `debounceMs` | Durée (ms) de stabilité requise avant de valider un changement d'état        | `50`   |

## Méthodes

| Méthode | Rôle |
|---|---|
| `begin()` | Configure la GPIO et initialise l'état stable. À appeler dans `setup()`. |
| `update()` | Lit la GPIO, applique l'anti-rebond et déclenche les callbacks. À appeler à chaque tour de `loop()`. |
| `onLow(callback)` | Fonction appelée quand l'état stable passe à `LOW`. |
| `onHigh(callback)` | Fonction appelée quand l'état stable passe à `HIGH`. |
| `isLow()` | `true` si l'état stable actuel est `LOW`. |
| `isHigh()` | `true` si l'état stable actuel est `HIGH`. |
| `getPin()` | Renvoie le numéro de GPIO du bouton. |

## Remarques

- Avec `usePullUp = true` (par défaut) et un bouton câblé entre la GPIO et
  la masse, `LOW` correspond à « appuyé » et `HIGH` à « relâché » — c'est le
  câblage du bouton BOOT de la plupart des cartes ESP32 (GPIO 0).
- Avec `usePullUp = false`, une résistance externe (pull-up ou pull-down)
  est nécessaire pour que la GPIO ait un état défini au repos ; la
  correspondance LOW/HIGH ↔ appuyé/relâché dépend alors du câblage choisi.
- `update()` doit être appelée régulièrement et sans délai bloquant
  (`delay()`) dans `loop()` pour que l'anti-rebond mesure correctement le
  temps écoulé.
- Une valeur `debounceMs` trop faible peut laisser passer des rebonds
  mécaniques (faux déclenchements) ; une valeur trop élevée retarde la
  détection d'un appui réel. 50 ms convient à la plupart des boutons.
