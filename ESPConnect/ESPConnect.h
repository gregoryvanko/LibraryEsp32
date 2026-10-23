/*
 * ESPConnect — gestion de la connexion WiFi, d'un portail captif, d'une page
 * de statut, d'un client MQTT (TCP/TLS/WS/WSS) et d'un serveur web avec
 * WebSocket pour ESP32 (framework Arduino).
 *
 * Voir README.md pour la description complète.
 */
#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <functional>
#include <map>
#include <vector>

#include "mqtt_client.h"

#ifndef ESPCONNECT_AP_IP
#define ESPCONNECT_AP_IP 192, 168, 1, 1
#endif

class ESPConnect {
public:
  using MqttMessageCallback = std::function<void(const String &topic, const String &payload)>;
  using WsMessageCallback = std::function<void(uint32_t clientId, const String &message)>;

  enum class WifiState : uint8_t { Idle, Portal, Connecting, Connected, Reconnecting };
  enum class MqttState : uint8_t { Disabled, Connecting, Connected, Error };

  ESPConnect();

  // --- Cycle de vie ---------------------------------------------------------
  // deviceName : nom d'appareil (hostname, topic "<nom>/status", clientId MQTT).
  //              La valeur enregistrée depuis la page /status est prioritaire.
  // apName     : nom du point d'accès ouvert du portail de configuration.
  void begin(const char *deviceName = "ESPConnect", const char *apName = "ESPConnect");
  void loop();  // à appeler à chaque passage dans loop()

  // --- Réglages (à appeler avant begin()) -----------------------------------
  void setResetButton(int8_t pin = 0, uint32_t holdMs = 2000);  // pin = -1 : désactivé
  void setStatusInterval(uint32_t ms = 60000);                  // période de "online"
  void setMaxConnectAttempts(uint8_t attempts = 5, uint32_t attemptTimeoutMs = 15000);
  void setReconnectInterval(uint32_t ms = 10000);               // retente du WiFi perdu
  // Mise à jour du firmware par le réseau (ArduinoOTA / espota), active dès que le WiFi est connecté.
  // password vide ou nullptr : aucune authentification (déconseillé).
  void enableOTA(const char *password, uint16_t port = 3232);

  // --- État -----------------------------------------------------------------
  WifiState wifiState() const { return _wifiState; }
  MqttState mqttState() const { return _mqttState; }
  bool isWifiConnected() const { return _wifiState == WifiState::Connected; }
  bool isMqttConnected() const { return _mqttState == MqttState::Connected; }
  bool isPortalActive() const { return _wifiState == WifiState::Portal; }
  bool isOTAEnabled() const { return _otaEnabled; }
  String deviceName() const;
  String statusTopic() const;

  // --- Effacement -----------------------------------------------------------
  void resetWifi();  // efface les identifiants WiFi et redémarre en mode portail
  void clearMqtt();  // ferme la connexion au broker et efface la configuration MQTT

  // --- MQTT -----------------------------------------------------------------
  // Les souscriptions sont mémorisées et renouvelées à chaque (re)connexion.
  bool subscribe(const String &topic, uint8_t qos = 0);
  bool unsubscribe(const String &topic);
  void onMqttMessage(MqttMessageCallback cb) { _mqttCallback = cb; }

  bool publish(const String &topic, const String &payload, bool retain = false, uint8_t qos = 0);
  bool publish(const String &topic, const char *payload, bool retain = false, uint8_t qos = 0);
  bool publish(const String &topic, bool value, bool retain = false, uint8_t qos = 0);
  bool publish(const String &topic, int value, bool retain = false, uint8_t qos = 0);
  bool publish(const String &topic, unsigned int value, bool retain = false, uint8_t qos = 0);
  bool publish(const String &topic, long value, bool retain = false, uint8_t qos = 0);
  bool publish(const String &topic, unsigned long value, bool retain = false, uint8_t qos = 0);
  bool publish(const String &topic, float value, bool retain = false, uint8_t qos = 0, uint8_t decimals = 2);
  bool publish(const String &topic, double value, bool retain = false, uint8_t qos = 0, uint8_t decimals = 2);

  // --- Routes web -----------------------------------------------------------
  // Sert une page stockée en mémoire (chaîne PROGMEM / const char[]).
  // Exemple : espConnect.addRoute("/semis", HTTP_GET, SEMIS_HTML);
  void addRoute(const char *path, WebRequestMethodComposite method, const char *content,
                const char *contentType = "text/html");

  // --- WebSocket (point d'accès "/ws") --------------------------------------
  // Le callback est appelé depuis loop() avec l'identifiant du client et le texte reçu.
  void onWsMessage(WsMessageCallback cb) { _wsCallback = cb; }
  bool wsSend(const String &message);                     // à tous les clients
  bool wsSend(uint32_t clientId, const String &message);  // à un client
  size_t wsClientCount() const { return _ws.count(); }

  // Accès direct au serveur (routes avancées).
  AsyncWebServer &server() { return _server; }

private:
  struct Route {
    String path;
    WebRequestMethodComposite method;
    const char *content;
    String contentType;
  };

  struct MqttSettings {
    bool enabled = false;
    String scheme = "wss";
    String host;
    uint16_t port = 443;
    String path = "/mqtt";
    String user;
    String pass;
  };

  struct InboundMessage {
    String topic;
    String payload;
    uint32_t clientId;  // 0 : message MQTT, sinon client WebSocket
  };

  // WiFi / portail
  void loadWifiCredentials();
  void startPortal();
  void startStation();
  void beginConnectAttempt();
  void handleWifi();
  void onWifiConnected();
  void handleResetButton();
  void setupPortalRoutes();
  void setupStationRoutes();
  void registerRoute(const Route &route);
  void startOTA();

  // MQTT
  void loadMqttSettings();
  void saveMqttSettings(const MqttSettings &s);
  void startMqtt();
  void stopMqtt(bool announceOffline);
  void applyPendingMqtt();
  bool rawPublish(const String &topic, const String &payload, bool retain, uint8_t qos);
  static String mqttUri(const MqttSettings &s);
  static void mqttEventHandler(void *arg, esp_event_base_t base, int32_t id, void *data);
  void onMqttEvent(esp_mqtt_event_handle_t event);

  // Web
  void handleStatusJson(AsyncWebServerRequest *request);
  void handleMqttPost(AsyncWebServerRequest *request);
  void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg,
                 uint8_t *data, size_t len);
  void pushInbound(InboundMessage *msg);
  void dispatchInbound();

  static String jsonEscape(const String &s);

  AsyncWebServer _server;
  AsyncWebSocket _ws;
  DNSServer _dns;
  SemaphoreHandle_t _lock = nullptr;
  QueueHandle_t _inbound = nullptr;

  String _defaultName;
  String _deviceName;
  String _apName;
  String _clientId;

  // WiFi
  WifiState _wifiState = WifiState::Idle;
  String _ssid;
  String _pass;
  bool _credsValid = false;
  uint8_t _maxAttempts = 5;
  uint32_t _attemptTimeout = 15000;
  uint32_t _reconnectInterval = 10000;
  uint8_t _attempt = 0;
  uint32_t _attemptStart = 0;
  bool _serverStarted = false;
  bool _rootRouteDefined = false;
  volatile uint32_t _restartAt = 0;
  volatile uint8_t _lastDisconnectReason = 0;

  // Bouton
  int8_t _btnPin = 0;
  uint32_t _btnHold = 2000;
  bool _btnPressed = false;
  bool _btnFired = false;
  uint32_t _btnStart = 0;

  // OTA
  bool _otaEnabled = false;
  bool _otaStarted = false;
  String _otaPassword;
  uint16_t _otaPort = 3232;
  int8_t _otaProgress = -1;

  // MQTT
  esp_mqtt_client_handle_t _mqtt = nullptr;
  MqttSettings _mqttCfg;
  MqttSettings _pendingMqtt;
  String _pendingName;
  volatile bool _mqttApplyPending = false;
  volatile bool _mqttClearPending = false;
  volatile MqttState _mqttState = MqttState::Disabled;
  volatile bool _mqttConnectedEvent = false;
  String _mqttError;
  String _rxTopic;  // réassemblage des messages fragmentés
  String _rxPayload;
  uint32_t _statusInterval = 60000;
  uint32_t _lastStatusPublish = 0;
  std::map<String, uint8_t> _subscriptions;

  // Web
  std::vector<Route> _routes;
  std::map<uint32_t, String> _wsBuffers;

  MqttMessageCallback _mqttCallback;
  WsMessageCallback _wsCallback;
};

extern ESPConnect espConnect;
