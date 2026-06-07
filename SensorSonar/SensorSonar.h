#pragma once
#include <Arduino.h>

class SensorSonar {
  public:
    /**
     * @brief Constructeur
     * @param trigPin   Numéro de broche GPIO pour TRIG
     * @param echoPin   Numéro de broche GPIO pour ECHO
     * @param maxDistCm Distance maximale mesurable en cm (défaut : 400)
     * @param timeoutUs Timeout en microsecondes (défaut : 30 000 µs)
     */
    explicit SensorSonar(uint8_t trigPin, uint8_t echoPin, uint16_t maxDistCm = 400, uint32_t timeoutUs = 30000);

    /**
     * @brief Initialise les broches GPIO. À appeler dans setup().
     */
    void begin();
 
    /**
     * @brief Effectue une mesure de distance.
     * @return Distance en centimètres, ou -1.0 si hors portée / timeout.
     */
    float measureCm();
 
    /**
     * @brief Effectue N mesures et retourne la médiane (filtre anti-bruit).
     * @param samples Nombre d'échantillons (3 à 9 recommandé)
     * @param delayMs Délai en ms entre chaque mesure
     * @return Médiane en centimètres, ou -1.0 si toutes les mesures échouent.
     */
    float measureMedianCm(uint8_t samples = 5, uint16_t delayMs = 30);
  
  private:
    uint8_t  _trigPin;
    uint8_t  _echoPin;
    uint16_t _maxDistCm;
    uint32_t _timeoutUs;

    /** Envoie une impulsion TRIG et retourne la durée ECHO en µs (0 = timeout). */
    uint32_t _pulseEcho();
 
    /** Trie un tableau float sur place (tri à bulles, taille ≤ 9). */
    static void _bubbleSort(float* arr, uint8_t n);
 
    static constexpr float SOUND_SPEED_CM_US = 0.034329f; // cm/µs à 20 °C 
};