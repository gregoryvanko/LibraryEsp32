# SensorDS18B20

Librairie ESP32 (framework Arduino, PlatformIO) pour lire plusieurs sondes de température DS18B20 branchées sur un même bus 1-Wire :

- découverte automatique des sondes présentes sur le bus (identifiées par leur adresse 1-Wire) ;
- mesure **non bloquante** : la conversion est lancée sur toutes les sondes en même temps, les résultats sont lus par `loop()` puis transmis à un callback ;
- détection des erreurs de lecture (sonde débranchée, CRC invalide, valeur de mise sous tension 85 °C) ;
- nom libre par sonde et export JSON de l'ensemble des mesures.

## Dépendances

À déclarer dans `platformio.ini` :

```ini
lib_deps =
	paulstoffregen/OneWire @ 2.3.8
	milesburton/DallasTemperature @ 4.0.4
```

## Câblage

| DS18B20       | ESP32                        |
|---------------|------------------------------|
| GND (noir)    | GND                          |
| VDD (rouge)   | 3,3 V                        |
| DQ (jaune)    | GPIO du bus (ex. GPIO 4)     |

Une **résistance de tirage de 4,7 kΩ** entre DQ et 3,3 V est indispensable (une seule pour tout le bus). Toutes les sondes sont branchées en parallèle sur les trois mêmes fils. Pour de grandes longueurs de câble, réduire la résistance (2,2 kΩ) ou la résolution.

Éviter les GPIO 6 à 11 (flash), 34 à 39 (entrées seules) et les broches de démarrage (0, 2, 12, 15).

## Exemple

```cpp
#include <Arduino.h>
#include <SensorDS18B20.h>

SensorDS18B20 sondes(4);  // bus 1-Wire sur GPIO 4

void setup() {
  Serial.begin(115200);

  sondes.onMeasure([](const SensorDS18B20 &s) {
    for (const SensorDS18B20::Sensor &sonde : s.sensors()) {
      if (sonde.valid) Serial.printf("%s : %.2f °C\n", sonde.name.c_str(), sonde.temperature);
    }
    Serial.println(s.toJson());
  });

  sondes.begin(12);                             // résolution 12 bits
  sondes.setName("28FF4A1B6316045C", "serre");  // nom optionnel
}

void loop() {
  sondes.loop();

  static uint32_t last = 0;
  if (millis() - last >= 10000) {
    last = millis();
    sondes.requestMeasure();
  }
}
```

## API

### Cycle de vie

| Méthode | Description |
|---|---|
| `SensorDS18B20(uint8_t pin)` | Crée le bus 1-Wire sur la broche `pin`. |
| `uint8_t begin(uint8_t resolution = 12)` | Initialise le bus, recherche les sondes et applique la résolution. Retourne le nombre de sondes. |
| `void loop()` | À appeler à chaque passage dans `loop()` : lit les résultats quand la conversion est terminée et appelle le callback. |

### Découverte et réglages

| Méthode | Description |
|---|---|
| `uint8_t scan()` | Relance la recherche des sondes (après ajout ou retrait). Les noms déjà attribués sont conservés. |
| `uint8_t count()` | Nombre de sondes connues. |
| `bool parasitePower()` | `true` si au moins une sonde est en alimentation parasite (VDD relié à GND). |
| `void setResolution(uint8_t bits)` | Résolution de 9 à 12 bits (voir tableau ci-dessous). |
| `bool setName(uint8_t index, const String &name)` | Nomme une sonde par son index. |
| `bool setName(const String &id, const String &name)` | Nomme une sonde par son adresse hexadécimale. |

| Résolution | Précision | Durée de conversion |
|---|---|---|
| 9 bits  | 0,5 °C    | 94 ms  |
| 10 bits | 0,25 °C   | 188 ms |
| 11 bits | 0,125 °C  | 375 ms |
| 12 bits | 0,0625 °C | 750 ms |

### Mesure

| Méthode | Description |
|---|---|
| `bool requestMeasure()` | Lance une conversion sur toutes les sondes, sans bloquer. Retourne `false` si une mesure est déjà en cours ou si aucune sonde n'est présente (un nouveau `scan()` est alors tenté). |
| `bool measure()` | Variante bloquante : lance la conversion, attend sa durée puis lit les résultats. |
| `bool isBusy()` | `true` pendant une conversion. |
| `uint32_t lastMeasure()` | `millis()` de la dernière lecture. |
| `void onMeasure(MeasureCallback cb)` | Callback `void(const SensorDS18B20 &)` appelé depuis `loop()` après chaque lecture. |

### Résultats

| Méthode | Description |
|---|---|
| `const Sensor &sensor(uint8_t index)` | Sonde à l'index donné. |
| `const std::vector<Sensor> &sensors()` | Toutes les sondes. |
| `float temperature(uint8_t index)` / `temperature(const String &id)` | Dernière température en °C, `NAN` en cas d'erreur. |
| `int indexOf(const String &id)` | Index d'une sonde par son adresse, `-1` si inconnue. |
| `String toJson(uint8_t decimals = 2)` | `[{"id":"28FF4A1B6316045C","name":"serre","temperature":21.50,"valid":true}, …]` (`null` si la lecture est en erreur). |
| `static String addressToString(const DeviceAddress)` | Adresse 1-Wire en hexadécimal (16 caractères). |

Structure `Sensor` :

| Champ | Description |
|---|---|
| `address` | Adresse 1-Wire (8 octets). |
| `id` | Adresse en hexadécimal, identifiant stable de la sonde. |
| `name` | Nom libre (par défaut : `id`). |
| `temperature` | Dernière température en °C (`NAN` si invalide). |
| `valid` | `false` si la dernière lecture est en erreur. |
| `errors` | Nombre de lectures en erreur consécutives. |

## Options de compilation

| Macro | Défaut | Description |
|---|---|---|
| `SENSORDS18B20_MAX_SENSORS` | `16` | Nombre maximum de sondes retenues par `scan()`. |
| `SENSORDS18B20_NO_LOG` | — | Désactive les messages sur le port série. |

À définir dans `platformio.ini`, par exemple : `build_flags = -DSENSORDS18B20_NO_LOG`.

## Remarques

- L'ordre des sondes suit l'ordre de recherche 1-Wire (adresses croissantes), pas l'ordre de branchement : utiliser l'`id` pour reconnaître une sonde.
- Une sonde débranchée reste dans la liste avec `valid = false` jusqu'au prochain `scan()`.
- Une valeur de 85 °C exacte est considérée comme une erreur (valeur par défaut du capteur avant toute conversion).
