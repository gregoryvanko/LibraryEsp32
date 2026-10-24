#include "SensorDS18B20.h"

#ifdef SENSORDS18B20_NO_LOG
#define DS_LOG(...) \
  do {              \
  } while (0)
#else
#define DS_LOG(fmt, ...) Serial.printf("[SensorDS18B20] " fmt "\n", ##__VA_ARGS__)
#endif

SensorDS18B20::SensorDS18B20(uint8_t pin) : _oneWire(pin), _dallas(&_oneWire) {}

uint8_t SensorDS18B20::begin(uint8_t resolution) {
  _dallas.begin();
  _dallas.setWaitForConversion(false);  // conversion asynchrone, gérée par loop()
  _resolution = constrain(resolution, 9, 12);
  scan();
  return count();
}

uint8_t SensorDS18B20::scan() {
  std::vector<Sensor> found;
  DeviceAddress addr;

  _oneWire.reset_search();
  while (found.size() < SENSORDS18B20_MAX_SENSORS && _oneWire.search(addr)) {
    if (OneWire::crc8(addr, 7) != addr[7]) continue;  // adresse corrompue
    if (!_dallas.validFamily(addr)) continue;         // autre composant 1-Wire

    Sensor s;
    memcpy(s.address, addr, sizeof(DeviceAddress));
    s.id = addressToString(addr);
    int previous = indexOf(s.id);
    s.name = previous >= 0 ? _sensors[previous].name : s.id;
    found.push_back(s);
  }

  _sensors = found;
  _busy = false;
  setResolution(_resolution);

  DS_LOG("%u sonde(s) trouvée(s)%s", count(), parasitePower() ? " (alimentation parasite)" : "");
  for (const Sensor &s : _sensors) DS_LOG("  %s (%s)", s.id.c_str(), s.name.c_str());
  return count();
}

void SensorDS18B20::setResolution(uint8_t bits) {
  _resolution = constrain(bits, 9, 12);
  for (Sensor &s : _sensors) _dallas.setResolution(s.address, _resolution);
  _conversionMs = _dallas.millisToWaitForConversion(_resolution);
}

bool SensorDS18B20::setName(uint8_t index, const String &name) {
  if (index >= _sensors.size()) return false;
  _sensors[index].name = name;
  return true;
}

bool SensorDS18B20::setName(const String &id, const String &name) {
  int index = indexOf(id);
  return index >= 0 && setName(index, name);
}

bool SensorDS18B20::requestMeasure() {
  if (_busy) return false;
  if (_sensors.empty() && scan() == 0) return false;

  _dallas.requestTemperatures();
  _requestAt = millis();
  _busy = true;
  return true;
}

bool SensorDS18B20::measure() {
  if (!requestMeasure()) return false;
  delay(_conversionMs);
  loop();
  return true;
}

void SensorDS18B20::loop() {
  if (!_busy || millis() - _requestAt < _conversionMs) return;
  _busy = false;
  readAll();
  if (_callback) _callback(*this);
}

void SensorDS18B20::readAll() {
  for (Sensor &s : _sensors) {
    float t = _dallas.getTempC(s.address);
    // -127 : sonde déconnectée ou CRC invalide. 85 °C : valeur de mise sous tension,
    // renvoyée lorsque la conversion n'a pas eu lieu (alimentation insuffisante).
    if (t == DEVICE_DISCONNECTED_C || t == 85.0f) {
      s.valid = false;
      s.temperature = NAN;
      s.errors++;
      DS_LOG("Lecture en erreur : %s (%s)", s.id.c_str(), s.name.c_str());
    } else {
      s.valid = true;
      s.temperature = t;
      s.errors = 0;
    }
  }
  _lastMeasure = millis();
}

float SensorDS18B20::temperature(uint8_t index) const {
  if (index >= _sensors.size() || !_sensors[index].valid) return NAN;
  return _sensors[index].temperature;
}

float SensorDS18B20::temperature(const String &id) const {
  int index = indexOf(id);
  return index >= 0 ? temperature(index) : NAN;
}

int SensorDS18B20::indexOf(const String &id) const {
  for (size_t i = 0; i < _sensors.size(); i++)
    if (_sensors[i].id.equalsIgnoreCase(id)) return i;
  return -1;
}

String SensorDS18B20::toJson(uint8_t decimals) const {
  String json = "[";
  for (size_t i = 0; i < _sensors.size(); i++) {
    const Sensor &s = _sensors[i];
    if (i) json += ',';
    json += "{\"id\":\"" + s.id + "\",\"name\":\"" + s.name + "\",\"temperature\":";
    json += s.valid ? String(s.temperature, (unsigned int)decimals) : String("null");
    json += ",\"valid\":";
    json += s.valid ? "true" : "false";
    json += '}';
  }
  json += ']';
  return json;
}

String SensorDS18B20::addressToString(const DeviceAddress address) {
  char buf[17];
  for (uint8_t i = 0; i < 8; i++) sprintf(buf + i * 2, "%02X", address[i]);
  return String(buf);
}
