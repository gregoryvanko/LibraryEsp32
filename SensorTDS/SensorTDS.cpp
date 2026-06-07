#include "SensorTDS.h"

// ─────────────────────────────────────────────
//  Constructeur
// ─────────────────────────────────────────────
SensorTDS::SensorTDS(uint8_t pin,
                   float   vRef,
                   int     adcRes,
                   uint8_t numSamples)
    : _pin(pin),
      _vRef(vRef),
      _adcRes(adcRes),
      _numSamples(numSamples)
{}

// ─────────────────────────────────────────────
//  Initialisation
// ─────────────────────────────────────────────
void SensorTDS::begin()
{
    // L'ESP32 configure automatiquement les GPIO en entrée analogique
    // dès le premier appel à analogRead(). On s'assure juste que
    // l'atténuation est correcte pour couvrir 0–3.3 V.
    analogSetPinAttenuation(_pin, ADC_11db);   // plage 0–3.3 V
    analogReadResolution(12);                  // 12 bits → 0–4095
}

// ─────────────────────────────────────────────
//  Lecture de tension moyennée
// ─────────────────────────────────────────────
float SensorTDS::readVoltage() const
{
    long sum = 0;
    for (uint8_t i = 0; i < _numSamples; ++i) {
        sum += analogRead(_pin);
        delay(EC_SAMPLE_INTERVAL_MS);
    }
    float avgRaw = static_cast<float>(sum) / _numSamples;

    // Conversion brut → tension
    return avgRaw * _vRef / static_cast<float>(_adcRes);
}

// ─────────────────────────────────────────────
//  Conversion tension → TDS (polynôme CQRobot)
// ─────────────────────────────────────────────
float SensorTDS::voltageTotds(float voltage)
{
    // Formule issue de la documentation / librairie CQRobot :
    //   TDS = (133.42·V³ - 255.86·V² + 857.39·V) × 0.5
    return (133.42f * voltage * voltage * voltage
          - 255.86f * voltage * voltage
          + 857.39f * voltage) * 0.5f;
}

// ─────────────────────────────────────────────
//  Coefficient de compensation thermique
// ─────────────────────────────────────────────
float SensorTDS::tempCompensation(float temperatureC)
{
    // Formule standard : k = 1 + EC_TEMP_COEFF × (T - Tref)
    return 1.0f + EC_TEMP_COEFF * (temperatureC - EC_TEMP_REF);
}

// ─────────────────────────────────────────────
//  Lecture TDS (ppm) avec compensation
// ─────────────────────────────────────────────
float SensorTDS::readTDS(float temperatureC) const
{
    float voltage   = readVoltage();
    float coeff     = tempCompensation(temperatureC);
    _lastTempCoeff  = coeff;

    // Tension compensée ramenée à 25 °C
    float voltageComp = voltage / coeff;

    return voltageTotds(voltageComp);
}

// ─────────────────────────────────────────────
//  Lecture EC (µS/cm) avec compensation
// ─────────────────────────────────────────────
float SensorTDS::readEC(float temperatureC) const
{
    // EC (µS/cm) ≈ TDS (ppm) × 2
    // (facteur de conversion eau douce usuel)
    return readTDS(temperatureC) * 2.0f;
}

// ─────────────────────────────────────────────
//  Accesseur : dernier coefficient thermique
// ─────────────────────────────────────────────
float SensorTDS::lastTemperatureCoefficient() const
{
    return _lastTempCoeff;
}