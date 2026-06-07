#include "MqttGateway.h"
#include <Arduino.h>

// Initialisation du membre statique
MqttGateway* MqttGateway::_instance = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Constructeur
// ─────────────────────────────────────────────────────────────────────────────
MqttGateway::MqttGateway(WiFiClient& wifiClient,
                         const char* brokerHost,
                         uint16_t    brokerPort,
                         const char* clientId)
    : _client(wifiClient),
      _brokerHost(brokerHost),
      _brokerPort(brokerPort),
      _clientId(clientId)
{
    // Enregistre l'instance pour le callback statique
    _instance = this;

    _client.setServer(_brokerHost, _brokerPort);
    _client.setCallback(_staticCallback);
    // Taille max d'un paquet MQTT (header + topic + payload). À ajuster si besoin.
    _client.setBufferSize(512);
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration optionnelle
// ─────────────────────────────────────────────────────────────────────────────
void MqttGateway::setCredentials(const char* username, const char* password) {
    _username = username;
    _password = password;
}

void MqttGateway::setLastWill(const char* topic,
                               const char* message,
                               bool        retain,
                               uint8_t     qos) {
    _lwtTopic   = topic;
    _lwtMessage = message;
    _lwtRetain  = retain;
    _lwtQos     = qos;
}

void MqttGateway::setMessageCallback(MqttMessageCallback callback) {
    _userCallback = callback;
}

// ─────────────────────────────────────────────────────────────────────────────
// Connexion
// ─────────────────────────────────────────────────────────────────────────────
bool MqttGateway::connect(uint8_t retries) {
    uint8_t attempts = 0;

    while (!_client.connected()) {
        Serial.printf("[MQTT] Connexion à %s:%u en tant que '%s'…\n",
                      _brokerHost, _brokerPort, _clientId.c_str());

        bool ok = false;

        if (_lwtTopic) {
            // Connexion avec LWT
            ok = (_username)
                ? _client.connect(_clientId.c_str(),
                                  _username, _password,
                                  _lwtTopic, _lwtQos, _lwtRetain, _lwtMessage)
                : _client.connect(_clientId.c_str(),
                                  nullptr, nullptr,
                                  _lwtTopic, _lwtQos, _lwtRetain, _lwtMessage);
        } else {
            ok = (_username)
                ? _client.connect(_clientId.c_str(), _username, _password)
                : _client.connect(_clientId.c_str());
        }

        if (ok) {
            Serial.println("[MQTT] Connecté.");
            return true;
        }

        Serial.printf("[MQTT] Échec (état=%d). Nouvelle tentative dans %lu ms…\n",
                      _client.state(), RECONNECT_INTERVAL_MS);

        if (retries > 0 && ++attempts >= retries) {
            Serial.println("[MQTT] Nombre maximum de tentatives atteint.");
            return false;
        }

        delay(RECONNECT_INTERVAL_MS);
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Souscription / désabonnement
// ─────────────────────────────────────────────────────────────────────────────
bool MqttGateway::subscribe(const char* topic, uint8_t qos) {
    if (!_client.connected()) {
        Serial.println("[MQTT] subscribe() : non connecté.");
        return false;
    }
    bool ok = _client.subscribe(topic, qos);
    Serial.printf("[MQTT] subscribe('%s', qos=%u) → %s\n",
                  topic, qos, ok ? "OK" : "ERREUR");
    return ok;
}

bool MqttGateway::unsubscribe(const char* topic) {
    if (!_client.connected()) return false;
    bool ok = _client.unsubscribe(topic);
    Serial.printf("[MQTT] unsubscribe('%s') → %s\n", topic, ok ? "OK" : "ERREUR");
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Publication
// ─────────────────────────────────────────────────────────────────────────────
bool MqttGateway::publish(const char*   topic,
                           const String& payload,
                           bool          retain,
                           uint8_t       qos) {
    return publish(topic, payload.c_str(), retain, qos);
}

bool MqttGateway::publish(const char* topic,
                           const char* payload,
                           bool        retain,
                           uint8_t     qos) {
    if (!_client.connected()) {
        Serial.println("[MQTT] publish() : non connecté.");
        return false;
    }

    bool ok = _client.publish(topic, payload, retain);
    Serial.printf("[MQTT] publish('%s', '%s', retain=%d) → %s\n",
                  topic, payload, retain, ok ? "OK" : "ERREUR");
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop — maintien de la connexion + traitement des messages
// ─────────────────────────────────────────────────────────────────────────────
void MqttGateway::loop() {
    _reconnectIfNeeded();
    _client.loop();
}

// ─────────────────────────────────────────────────────────────────────────────
// État
// ─────────────────────────────────────────────────────────────────────────────
bool MqttGateway::isConnected() {
    return _client.connected();
}

int MqttGateway::stateCode() {
    return _client.state();
}

// ─────────────────────────────────────────────────────────────────────────────
// Reconnexion automatique non-bloquante
// ─────────────────────────────────────────────────────────────────────────────
void MqttGateway::_reconnectIfNeeded() {
    if (_client.connected()) return;

    unsigned long now = millis();
    if (now - _lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
        _lastReconnectAttempt = now;
        Serial.println("[MQTT] Connexion perdue, tentative de reconnexion…");
        connect(1); // 1 seule tentative par appel de loop()
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Callback statique → redirige vers l'instance
// ─────────────────────────────────────────────────────────────────────────────
void MqttGateway::_staticCallback(char* topic, byte* payload, unsigned int length) {
    if (!_instance || !_instance->_userCallback) return;

    String topicStr(topic);
    String payloadStr;
    payloadStr.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += static_cast<char>(payload[i]);
    }

    _instance->_userCallback(topicStr, payloadStr);
}
