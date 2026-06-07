#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
//  Configuration par défaut (modifiable)
// ─────────────────────────────────────────────
#ifndef EC_VREF
  #define EC_VREF             3.3f      // Tension de référence ADC (V)
#endif
 
#ifndef EC_ADC_RESOLUTION
  #define EC_ADC_RESOLUTION   4096      // 12 bits
#endif
 
#ifndef EC_NUM_SAMPLES
  #define EC_NUM_SAMPLES      20        // Moyennage des lectures ADC
#endif
 
#ifndef EC_SAMPLE_INTERVAL_MS
  #define EC_SAMPLE_INTERVAL_MS 40      // Délai entre chaque échantillon (ms)
#endif

// ─────────────────────────────────────────────
//  Constantes de conversion
// ─────────────────────────────────────────────
 
// Coefficient de compensation thermique (%/°C, ref 25 °C)
constexpr float EC_TEMP_COEFF = 0.02f;
 
// Référence de température (°C)
constexpr float EC_TEMP_REF   = 25.0f;

/**
 * @brief Classe de mesure de la conductivité électrique (EC)
 *        via le capteur TDS CQRobot sur ESP32.
 *
 * Unités :
 *   - TDS  : ppm  (mg/L)
 *   - EC   : µS/cm
 *   - Relation approx. : EC (µS/cm) ≈ TDS (ppm) × 2
 */
class SensorTDS {
public:
    /**
     * @param pin         Broche ADC connectée au capteur
     * @param vRef        Tension de référence de l'ADC (V)
     * @param adcRes      Résolution de l'ADC (ex. 4096 pour 12 bits)
     * @param numSamples  Nombre d'échantillons pour le moyennage
     */
    explicit SensorTDS(uint8_t pin,
                      float   vRef       = EC_VREF,
                      int     adcRes     = EC_ADC_RESOLUTION,
                      uint8_t numSamples = EC_NUM_SAMPLES);
 
    /**
     * @brief Initialise la broche ADC.
     *        À appeler dans setup().
     */
    void begin();
 
    /**
     * @brief Lit la tension brute moyennée (V).
     */
    float readVoltage() const;
 
    /**
     * @brief Calcule la valeur TDS (ppm) avec compensation thermique.
     * @param temperatureC  Température de l'eau en °C (défaut 25 °C).
     */
    float readTDS(float temperatureC = EC_TEMP_REF) const;
 
    /**
     * @brief Calcule la conductivité électrique EC (µS/cm)
     *        avec compensation thermique.
     * @param temperatureC  Température de l'eau en °C (défaut 25 °C).
     */
    float readEC(float temperatureC = EC_TEMP_REF) const;
 
    /**
     * @brief Retourne le dernier coefficient de compensation
     *        thermique calculé (sans unité).
     */
    float lastTemperatureCoefficient() const;
 
    // ── Accesseurs de configuration ───────────────
    uint8_t pin()        const { return _pin; }
    float   vRef()       const { return _vRef; }
    int     adcRes()     const { return _adcRes; }
    uint8_t numSamples() const { return _numSamples; }
 
private:
    uint8_t _pin;
    float   _vRef;
    int     _adcRes;
    uint8_t _numSamples;
 
    mutable float _lastTempCoeff = 1.0f;
 
    /**
     * @brief Convertit une tension (V) en TDS brut (ppm, à 25 °C).
     *        Polynôme d'étalonnage fourni par CQRobot.
     */
    static float voltageTotds(float voltage);
 
    /**
     * @brief Calcule le coefficient de compensation thermique.
     */
    static float tempCompensation(float temperatureC);
};