# Strucutre de donnees
Exemple de strucutres de données

## Fichier main
```cpp
#include "SensorData.h"
void loop() {
  // Init Sensor Data
  SensorData_t data = {
      .waterTemp    = 0.0f,
      .waterEc      = 0.0f,
      .waterLevel   = 0.0f,
      .waterPresent = false,
      .timestamp    = millis()
  };
  // Mesure des capteurs
  data.timestamp = now;
  data.waterTemp = SensorTemperature1.read();
  data.waterPresent = SensorWaterPresent1.read();
  data.waterLevel = SensorSonar1.measureMedianCm(9);
  data.waterEc = SensorTDS1.readEC(data.waterTemp);
  // Print mesures
  Serial.printf("[DATA] Temp=%.2f°C  EC=%.2fmS/cm  Niveau=%.1fcm  Flux=%s\n",
                data.waterTemp,
                data.waterEc,
                data.waterLevel,
                data.waterPresent ? "OUI" : "NON");
  
  // Sérialisation data
  String dataSerialized = sensorDataToString(data);
}
```
