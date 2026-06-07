#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
//  Structure de données capteurs
// ─────────────────────────────────────────────
typedef struct {
    float    waterTemp;     // °C
    float    waterEc;       // mS/cm
    float    waterLevel;    // cm
    bool     waterPresent;  // true = flux détecté
    uint32_t timestamp;     // millis() au moment de l'envoi
} SensorData_t;


// ─────────────────────────────────────────────
//  Format sérialisé  (séparateur '|')
//  "waterTemp|waterEc|waterLevel|waterPresent|timestamp"
//  Exemple : "23.40|1.85|12.30|1|4823910"
// ─────────────────────────────────────────────

/**
 * @brief  Convertit un objet SensorData_t en String.
 *
 * @param  data   Référence constante vers la structure source.
 * @return String au format "T|EC|LVL|PRES|TS"
 */
inline String sensorDataToString(const SensorData_t& data)
{
    String s;
    s.reserve(48);                          // évite les réallocations

    s += String(data.waterTemp,    2); s += '|';
    s += String(data.waterEc,      2); s += '|';
    s += String(data.waterLevel,   2); s += '|';
    s += String(data.waterPresent ? 1 : 0); s += '|';
    s += String(data.timestamp);

    return s;
}

/**
 * @brief  Convertit une String en objet SensorData_t.
 *
 * @param  raw    String source au format "T|EC|LVL|PRES|TS".
 * @param  out    Référence vers la structure de destination.
 * @return true   si le parsing a réussi (5 champs trouvés),
 *         false  si la String est malformée.
 */
inline bool stringToSensorData(const String& raw, SensorData_t& out)
{
    // On travaille sur une copie mutable
    String buf = raw;
    buf.trim();

    // Découpage sur le séparateur '|'
    String fields[5];
    uint8_t idx   = 0;
    int     start = 0;

    for (int i = 0; i <= buf.length(); i++) {
        if (i == (int)buf.length() || buf[i] == '|') {
            if (idx >= 5) return false;         // trop de champs
            fields[idx++] = buf.substring(start, i);
            start = i + 1;
        }
    }

    if (idx != 5) return false;                 // nombre de champs incorrect

    out.waterTemp     = fields[0].toFloat();
    out.waterEc       = fields[1].toFloat();
    out.waterLevel    = fields[2].toFloat();
    out.waterPresent  = (fields[3].toInt() != 0);
    out.timestamp     = (uint32_t)fields[4].toInt();

    return true;
}
