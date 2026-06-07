#include "SensorSonar.h"

// ─────────────────────────────────────────────
//  Constructeur
// ─────────────────────────────────────────────
SensorSonar::SensorSonar(uint8_t  trigPin,
                     uint8_t  echoPin,
                     uint16_t maxDistCm,
                     uint32_t timeoutUs)
    : _trigPin(trigPin),
      _echoPin(echoPin),
      _maxDistCm(maxDistCm),
      _timeoutUs(timeoutUs)
{}

// ─────────────────────────────────────────────
//  Initialisation
// ─────────────────────────────────────────────
void SensorSonar::begin()
{
    pinMode(_trigPin, OUTPUT);
    pinMode(_echoPin, INPUT);
    digitalWrite(_trigPin, LOW);
    delay(100); // stabilisation capteur
}

// ─────────────────────────────────────────────
//  Mesure unique en centimètres
// ─────────────────────────────────────────────
float SensorSonar::measureCm()
{
    uint32_t duration = _pulseEcho();

    if (duration == 0) {
        return -1.0f;
    }

    float distCm = (duration * SOUND_SPEED_CM_US) / 2.0f;

    if (distCm > _maxDistCm) {
        return -1.0f;
    }

    return distCm;
}

// ─────────────────────────────────────────────
//  Mesure médiane (filtre bruit)
// ─────────────────────────────────────────────
float SensorSonar::measureMedianCm(uint8_t samples, uint16_t delayMs)
{
    // Clamp : entre 3 et 9 échantillons
    if (samples < 3) samples = 3;
    if (samples > 9) samples = 9;

    float readings[9];
    uint8_t validCount = 0;

    for (uint8_t i = 0; i < samples; i++) {
        float d = measureCm();
        if (d >= 0.0f) {
            readings[validCount++] = d;
        }
        if (i < samples - 1) {
            delay(delayMs);
        }
    }

    if (validCount == 0) {
        return -1.0f;
    }

    _bubbleSort(readings, validCount);
    return readings[validCount / 2]; // médiane
}

// ─────────────────────────────────────────────
//  Impulsion TRIG + lecture ECHO
// ─────────────────────────────────────────────
uint32_t SensorSonar::_pulseEcho()
{
    // S'assurer que TRIG est bas
    digitalWrite(_trigPin, LOW);
    delayMicroseconds(4);

    // Impulsion TRIG de 10 µs
    digitalWrite(_trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(_trigPin, LOW);

    // Lecture de la durée ECHO
    uint32_t duration = pulseIn(_echoPin, HIGH, _timeoutUs);

    return duration; // 0 si timeout
}

// ─────────────────────────────────────────────
//  Tri à bulles (tableau de taille ≤ 9)
// ─────────────────────────────────────────────
void SensorSonar::_bubbleSort(float* arr, uint8_t n)
{
    for (uint8_t i = 0; i < n - 1; i++) {
        for (uint8_t j = 0; j < n - 1 - i; j++) {
            if (arr[j] > arr[j + 1]) {
                float tmp  = arr[j];
                arr[j]     = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }
}