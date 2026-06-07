#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <functional>

// Callback type pour les messages reçus
// Paramètres : topic, payload, longueur du payload
using MqttMessageCallback = std::function<void(const String& topic, const String& payload)>;

class MqttGateway {
public:
    /**
     * @brief Constructeur
     * @param wifiClient  Référence à un WiFiClient déjà associé à une connexion WiFi active
     * @param brokerHost  Adresse IP ou hostname du broker MQTT
     * @param brokerPort  Port du broker (défaut : 1883)
     * @param clientId    Identifiant unique du client MQTT (ex: "ESP32_NFT_Tank1")
     */
    MqttGateway(WiFiClient& wifiClient,
                const char* brokerHost,
                uint16_t    brokerPort = 1883,
                const char* clientId   = "ESP32_Hydro");

    /**
     * @brief (Optionnel) Authentification sur le broker
     */
    void setCredentials(const char* username, const char* password);

    /**
     * @brief (Optionnel) Dernier message testament (LWT)
     * @param topic   Topic du LWT
     * @param message Payload du LWT
     * @param retain  Retained flag
     * @param qos     QoS (0 ou 1)
     */
    void setLastWill(const char* topic,
                     const char* message,
                     bool        retain = true,
                     uint8_t     qos    = 1);

    /**
     * @brief Enregistre le callback global appelé pour tout message entrant
     */
    void setMessageCallback(MqttMessageCallback callback);

    /**
     * @brief Tente la connexion au broker. Doit être appelé après WiFi.begin().
     * @param retries Nombre de tentatives avant d'abandonner (0 = infini)
     * @return true si connecté
     */
    bool connect(uint8_t retries = 5);

    /**
     * @brief Souscrit à un topic
     * @param topic Topic MQTT (wildcards + et # supportés)
     * @param qos   Niveau QoS (0 ou 1)
     * @return true si la souscription a réussi
     */
    bool subscribe(const char* topic, uint8_t qos = 0);

    /**
     * @brief Se désabonne d'un topic
     */
    bool unsubscribe(const char* topic);

    /**
     * @brief Publie un message String sur un topic
     * @param topic   Topic de destination
     * @param payload Message à publier
     * @param retain  Retained flag (défaut : false)
     * @param qos     QoS (0 ou 1 ; PubSubClient ne supporte pas QoS 2)
     * @return true si publié avec succès
     */
    bool publish(const char* topic,
                 const String& payload,
                 bool        retain = false,
                 uint8_t     qos    = 0);

    /**
     * @brief Surcharge pratique avec const char*
     */
    bool publish(const char* topic,
                 const char* payload,
                 bool        retain = false,
                 uint8_t     qos    = 0);

    /**
     * @brief À appeler dans loop() — maintient la connexion et traite les messages entrants
     */
    void loop();

    /** @return true si actuellement connecté au broker */
    bool isConnected();

    /** @return Code d'état PubSubClient (utile pour le debug) */
    int stateCode();

private:
    // Reconnexion automatique non-bloquante
    void _reconnectIfNeeded();

    // Adaptateur statique pour PubSubClient (qui n'accepte pas std::function)
    static void _staticCallback(char* topic, byte* payload, unsigned int length);
    static MqttGateway* _instance; // Pointeur vers l'instance courante pour le callback statique

    PubSubClient        _client;
    const char*         _brokerHost;
    uint16_t            _brokerPort;
    String              _clientId;
    const char*         _username     = nullptr;
    const char*         _password     = nullptr;
    const char*         _lwtTopic     = nullptr;
    const char*         _lwtMessage   = nullptr;
    bool                _lwtRetain    = true;
    uint8_t             _lwtQos       = 1;
    MqttMessageCallback _userCallback = nullptr;

    unsigned long       _lastReconnectAttempt = 0;
    static constexpr unsigned long RECONNECT_INTERVAL_MS = 5000;
};
