/*
 * SensorDS18B20 — lecture de plusieurs sondes de température DS18B20 sur un
 * même bus 1-Wire pour ESP32 (framework Arduino).
 *
 * La mesure est non bloquante : requestMeasure() lance la conversion sur toutes
 * les sondes, loop() lit les résultats une fois la conversion terminée puis
 * appelle le callback onMeasure().
 *
 * Voir README.md pour la description complète.
 */
#pragma once

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <functional>
#include <vector>

#ifndef SENSORDS18B20_MAX_SENSORS
#define SENSORDS18B20_MAX_SENSORS 16
#endif

class SensorDS18B20 {
public:
  struct Sensor {
    DeviceAddress address;  // adresse 1-Wire (8 octets)
    String id;              // adresse en hexadécimal (16 caractères), ex. "28FF4A1B6316045C"
    String name;            // nom libre (par défaut : id)
    float temperature = NAN;
    bool valid = false;     // false : sonde absente ou erreur de lecture (CRC, déconnexion)
    uint32_t errors = 0;    // nombre de lectures en erreur consécutives
  };

  using MeasureCallback = std::function<void(const SensorDS18B20 &sensors)>;

  explicit SensorDS18B20(uint8_t pin);

  // --- Cycle de vie ---------------------------------------------------------
  // resolution : 9 à 12 bits (conversion de 94 ms à 750 ms). Retourne le nombre de sondes trouvées.
  uint8_t begin(uint8_t resolution = 12);
  void loop();  // à appeler à chaque passage dans loop()

  // --- Découverte -----------------------------------------------------------
  // Recherche les sondes présentes sur le bus. Les noms déjà attribués sont conservés.
  uint8_t scan();
  uint8_t count() const { return _sensors.size(); }
  bool parasitePower() { return _dallas.isParasitePowerMode(); }

  // --- Réglages -------------------------------------------------------------
  void setResolution(uint8_t bits);
  uint8_t resolution() const { return _resolution; }
  bool setName(uint8_t index, const String &name);
  bool setName(const String &id, const String &name);

  // --- Mesure ---------------------------------------------------------------
  // Lance une conversion sur toutes les sondes (non bloquant). Retourne false si une mesure
  // est déjà en cours ou si aucune sonde n'est présente (un nouveau scan est alors tenté).
  bool requestMeasure();
  // Mesure bloquante : lance la conversion, attend et lit les résultats.
  bool measure();
  bool isBusy() const { return _busy; }
  uint32_t lastMeasure() const { return _lastMeasure; }  // millis() de la dernière lecture
  void onMeasure(MeasureCallback cb) { _callback = cb; }

  // --- Résultats ------------------------------------------------------------
  const Sensor &sensor(uint8_t index) const { return _sensors[index]; }
  const std::vector<Sensor> &sensors() const { return _sensors; }
  float temperature(uint8_t index) const;          // NAN si index invalide ou lecture en erreur
  float temperature(const String &id) const;
  int indexOf(const String &id) const;             // -1 si inconnu
  // Ex. : [{"id":"28FF4A1B6316045C","name":"serre","temperature":21.50,"valid":true}]
  String toJson(uint8_t decimals = 2) const;

  static String addressToString(const DeviceAddress address);

private:
  void readAll();

  OneWire _oneWire;
  DallasTemperature _dallas;
  std::vector<Sensor> _sensors;
  uint8_t _resolution = 12;
  bool _busy = false;
  uint32_t _requestAt = 0;
  uint32_t _conversionMs = 750;
  uint32_t _lastMeasure = 0;
  MeasureCallback _callback;
};
