#ifndef WIFI_AUTO_H
#define WIFI_AUTO_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <functional>

// Connexion WiFi + MQTT automatique pour ESP32.
//
// WiFi :
// - Sans identifiants en mémoire : l'ESP32 crée un point d'accès OUVERT (sans mot de passe).
//   Une fois connecté dessus, une page web (http://192.168.1.1, ouverte automatiquement
//   sur la plupart des téléphones) permet de saisir le SSID et le mot de passe du réseau.
// - Les identifiants sont enregistrés en mémoire flash (NVS) puis l'ESP32 redémarre
//   et se connecte au réseau. Ils sont retrouvés après une coupure de courant.
// - Une reconnexion automatique est gérée si le signal WiFi est perdu.
// - Un appui long sur le bouton (défaut : BOOT, GPIO 0) efface les identifiants
//   et redémarre l'ESP32 en mode point d'accès.
//
// MQTT :
// - Une fois connecté au WiFi, la page http://<IP>/config permet de saisir l'adresse du
//   broker MQTT, son port, un identifiant client et des identifiants optionnels. Cette
//   page n'est jamais servie en mode point d'accès.
// - Ces paramètres sont enregistrés en mémoire flash (NVS) et relus au démarrage.
// - La connexion au broker est gérée et reconnectée automatiquement, sans bloquer loop().
// - publish()/subscribe() permettent d'échanger des messages ; le callback de message
//   reçoit le topic et le payload (String).
// - Si un topic de statut est fourni au constructeur, l'ESP32 y publie périodiquement
//   "online" (retenu) tant qu'il est connecté, et le broker y affiche "offline" (retenu,
//   via le testament MQTT) si l'ESP32 se déconnecte brutalement.
//
// Utilisation :
//   WifiAuto wifi;                                    // WiFi seul
//   WifiAuto wifi(0, "MonESP32", "esp32/salon/status"); // + statut MQTT régulier
//   void setup() { wifi.begin(); }
//   void loop()  { wifi.update(); }
class WifiAuto {
public:
  using Callback = std::function<void()>;
  // Paramètres : topic, payload
  using MqttMessageCallback = std::function<void(const String& topic, const String& payload)>;

  // buttonPin       : GPIO du bouton de réinitialisation (défaut : 0 = bouton BOOT)
  // apName          : nom (SSID) du point d'accès de configuration
  // mqttStatusTopic : topic sur lequel publier régulièrement le statut de connexion
  //                   ("online"/"offline", retenu). Laisser nullptr pour désactiver.
  WifiAuto(uint8_t buttonPin = 0,
           const char* apName = "ESP32-WifiAuto",
           const char* mqttStatusTopic = nullptr);

  // Le WebServer et PubSubClient gardent des références internes : l'objet ne doit pas être copié
  WifiAuto(const WifiAuto&)            = delete;
  WifiAuto& operator=(const WifiAuto&) = delete;

  // --- Réglages WiFi optionnels (à appeler AVANT begin()) ---

  // Bouton actif à l'état bas (défaut, ex. BOOT) ou à l'état haut.
  // usePullUp : active la résistance de pull-up interne (uniquement si activeLow = true).
  void setButton(uint8_t pin, bool activeLow = true, bool usePullUp = true);
  // Durée d'appui (ms) pour effacer la configuration (défaut : 2000)
  void setResetHoldTime(uint32_t ms);
  // Délai (ms) entre deux tentatives de reconnexion (défaut : 10000)
  void setReconnectInterval(uint32_t ms);
  // Réseau tout juste saisi dans le portail (donc pas encore validé) : nombre de nouvelles
  // tentatives (défaut : 2) et durée (ms) de chaque tentative (défaut : 15000). Si elles
  // échouent toutes, les identifiants sont effacés et le portail est rouvert.
  // Un réseau déjà validé une fois n'est jamais effacé : on se reconnecte indéfiniment.
  void setRetryPolicy(uint8_t retries, uint32_t attemptTimeoutMs);
  // Active (défaut) ou coupe les logs sur le port série (WiFi et MQTT). Peut être appelé à tout moment.
  void setDebug(bool enabled);
  // Adresse IP de l'ESP32 en mode point d'accès (défaut : 192.168.1.1)
  void setPortalIP(IPAddress ip);
  // Nom d'hôte de l'ESP32 sur le réseau
  void setHostname(const char* hostname);

  // Fonctions appelées à la connexion / à la perte du WiFi
  void onConnected(Callback callback);
  void onDisconnected(Callback callback);

  // --- Réglages MQTT optionnels (à appeler AVANT begin()) ---

  // Identifiants d'authentification par défaut (écrasés si l'utilisateur en saisit
  // via la page /config)
  void setMqttCredentials(const char* username, const char* password);
  // Testament MQTT personnalisé, envoyé par le broker si l'ESP32 se déconnecte brutalement.
  // Si non appelé et qu'un topic de statut est défini au constructeur, un testament
  // automatique ("offline", retenu) est utilisé sur ce topic.
  void setMqttLastWill(const char* topic, const char* message, bool retain = true, uint8_t qos = 1);
  // Callback appelé pour chaque message reçu sur un topic souscrit
  void setMqttMessageCallback(MqttMessageCallback callback);
  // Délai (ms) entre deux tentatives de connexion au broker (défaut : 5000)
  void setMqttReconnectInterval(uint32_t ms);
  // Intervalle (ms) de publication régulière du statut de connexion (défaut : 60000)
  void setMqttStatusInterval(uint32_t ms);
  // Configuration MQTT tout juste saisie (donc pas encore validée) : nombre de tentatives
  // de connexion avant d'abandonner (défaut : 5). Si elles échouent toutes, la configuration
  // est considérée incorrecte : elle est effacée de la mémoire et les tentatives s'arrêtent.
  // Une configuration déjà validée une fois n'est jamais effacée : on se reconnecte indéfiniment.
  void setMqttMaxRetries(uint8_t retries);
  // Fonctions appelées à la connexion / à la perte de connexion au broker MQTT
  void onMqttConnected(Callback callback);
  void onMqttDisconnected(Callback callback);

  // Démarre la librairie : charge la configuration puis lance la connexion
  // (mode station) ou le portail de configuration (mode point d'accès).
  // Non bloquant. À appeler dans setup().
  void begin();

  // Gère le bouton, le portail, la reconnexion WiFi et MQTT. À appeler à chaque tour de loop().
  void update();

  // Efface le SSID et le mot de passe WiFi en mémoire, sans redémarrer
  void clearCredentials();
  // Efface la configuration WiFi puis redémarre l'ESP32 (relance en mode point d'accès)
  void resetAndRestart();

  // --- État WiFi ---
  bool isConnected() const;      // connecté au réseau WiFi
  bool isPortalActive() const;   // mode point d'accès / portail de configuration
  bool hasCredentials() const;   // SSID enregistré en mémoire
  String getSSID() const;        // SSID enregistré
  IPAddress getLocalIP() const;  // IP obtenue sur le réseau (ou IP du portail)
  // Dernier motif de déconnexion WiFi (code ESP-IDF, 0 = aucun) et sa description
  uint8_t getLastDisconnectReason() const;
  String  getLastDisconnectReasonText() const;

  // --- MQTT ---

  // Publie un message (String ou const char*) sur un topic.
  // qos est conservé pour compatibilité d'API : PubSubClient ne publie qu'en QoS 0.
  bool mqttPublish(const char* topic, const String& payload, bool retain = false, uint8_t qos = 0);
  bool mqttPublish(const char* topic, const char* payload, bool retain = false, uint8_t qos = 0);
  // Souscrit / se désabonne d'un topic (wildcards + et # supportés)
  bool mqttSubscribe(const char* topic, uint8_t qos = 0);
  bool mqttUnsubscribe(const char* topic);

  bool isMqttConnected();          // connecté au broker MQTT
  bool hasMqttConfig() const;      // un broker MQTT est enregistré en mémoire
  String getMqttHost() const;
  uint16_t getMqttPort() const;
  int getMqttStateCode();          // code d'état PubSubClient (utile pour le debug)
  String getMqttStateText();       // description du code d'état ci-dessus

  // Efface la configuration MQTT en mémoire et ferme la connexion au broker, sans redémarrer
  void clearMqttConfig();

private:
  // Configuration WiFi
  uint8_t  _buttonPin;
  bool     _buttonActiveLow = true;
  bool     _buttonPullUp    = true;
  uint32_t _resetHoldMs     = 2000;
  uint32_t _reconnectMs     = 10000;
  uint8_t  _maxRetries      = 2;
  uint32_t _attemptTimeoutMs = 15000;
  bool     _debug           = true;
  IPAddress _apIP           = IPAddress(192, 168, 1, 1);
  String   _apName;
  String   _hostname;

  // Identifiants WiFi chargés depuis la mémoire
  String _ssid;
  String _password;
  bool   _unverified = false;   // identifiants saisis mais jamais connectés avec succès
  String _failedSsid;           // dernier SSID rejeté, affiché sur le portail
  uint8_t _retryCount = 0;
  volatile uint8_t _lastDisconnectReason = 0;  // écrit par la tâche WiFi
  bool _eventRegistered = false;

  Preferences _prefs;
  WebServer   _server;
  DNSServer   _dns;

  bool     _portalActive = false;
  bool     _wasConnected = false;
  uint32_t _lastAttemptMs = 0;
  bool     _pressed        = false;
  uint32_t _pressStartMs   = 0;   // instant (ms) du début de l'appui
  bool     _restartPending = false;
  uint32_t _restartAtMs    = 0;   // instant (ms) du redémarrage planifié

  Callback _onConnected;
  Callback _onDisconnected;

  // Configuration MQTT
  WiFiClient   _mqttWifiClient;
  PubSubClient _mqttClient;

  String   _mqttStatusTopic;      // topic de statut, défini au constructeur (vide = désactivé)
  String   _mqttHost;
  uint16_t _mqttPort = 1883;
  String   _mqttClientId;
  String   _mqttUsername;
  String   _mqttPassword;

  String   _lwtTopic;             // testament personnalisé (setMqttLastWill), vide = automatique
  String   _lwtMessage;
  bool     _lwtRetain = true;
  uint8_t  _lwtQos     = 1;

  MqttMessageCallback _mqttMessageCallback;
  Callback _onMqttConnected;
  Callback _onMqttDisconnected;

  bool     _mqttWasConnected     = false;
  uint32_t _lastMqttAttemptMs    = 0;
  uint32_t _mqttReconnectMs      = 5000;
  uint32_t _lastMqttStatusMs     = 0;
  uint32_t _mqttStatusIntervalMs = 60000;

  bool    _mqttVerified   = false;  // la configuration MQTT a déjà réussi à se connecter au moins une fois
  uint8_t _mqttFailCount  = 0;      // tentatives infructueuses depuis la dernière config enregistrée
  uint8_t _mqttMaxRetries = 5;

  static WifiAuto* _instance;   // pour le callback statique PubSubClient (une seule instance)

  void loadCredentials();
  void saveCredentials(const String& ssid, const String& password);
  void markVerified();

  void debugPrintf(const char* format, ...) const __attribute__((format(printf, 2, 3)));
  void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info);
  static const char* disconnectReasonText(uint8_t reason);

  void startStation();
  void fallbackToPortal();
  void startPortal();

  void handleButton();
  void handleStation();

  void handleRoot();
  void handleSave();
  void handleNotFound();

  // MQTT
  void loadMqttConfig();
  void saveMqttConfig(const String& host, uint16_t port, const String& clientId,
                       const String& username, const String& password);
  void applyMqttConfig();
  void markMqttVerified();

  void handleMqtt();
  void mqttConnectAttempt();
  void publishMqttStatus();
  static String defaultMqttClientId();
  static const char* mqttStateTextFor(int state);
  static void _staticMqttCallback(char* topic, byte* payload, unsigned int length);

  void handleConfig();
  void handleConfigSave();
  void handleConfigClear();
};

#endif
