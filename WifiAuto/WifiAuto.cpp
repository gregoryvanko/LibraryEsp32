#include "WifiAuto.h"
#include <uri/UriGlob.h>
#include <stdarg.h>

// Espace de noms et clés en mémoire flash (NVS) — configuration WiFi
static const char* NVS_NAMESPACE = "wifiauto";
static const char* NVS_KEY_SSID  = "ssid";
static const char* NVS_KEY_PASS  = "pass";
static const char* NVS_KEY_UNVERIFIED = "unverified";  // identifiants jamais validés par une connexion

// Espace de noms et clés en mémoire flash (NVS) — configuration MQTT (séparé du WiFi :
// l'effacement des identifiants WiFi via le bouton ne doit pas effacer la config MQTT)
static const char* NVS_MQTT_NAMESPACE = "wifiauto_mqtt";
static const char* MQTT_KEY_HOST = "host";
static const char* MQTT_KEY_PORT = "port";
static const char* MQTT_KEY_CID  = "cid";
static const char* MQTT_KEY_USER = "user";
static const char* MQTT_KEY_PASS = "pass";
static const char* MQTT_KEY_VERIFIED = "verified";  // la config a déjà réussi à se connecter une fois

// Délai avant redémarrage après l'enregistrement WiFi, pour laisser partir la réponse HTTP
static const uint32_t RESTART_DELAY_MS = 1500;

// Taille max d'un paquet MQTT (header + topic + payload)
static const uint16_t MQTT_BUFFER_SIZE = 512;

static const char PAGE_HEAD[] PROGMEM =
  "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Configuration ESP32</title><style>"
  "body{font-family:sans-serif;background:#f2f2f2;margin:0;padding:16px}"
  ".card{max-width:380px;margin:24px auto;background:#fff;padding:20px;border-radius:10px;"
  "box-shadow:0 2px 8px rgba(0,0,0,.15)}"
  "h1{font-size:20px;margin-top:0}label{display:block;margin:14px 0 4px;font-size:14px}"
  "input[type=text],input[type=password],input[type=number]{width:100%;box-sizing:border-box;padding:10px;"
  "font-size:16px;border:1px solid #bbb;border-radius:6px}"
  "button{width:100%;margin-top:20px;padding:12px;font-size:16px;border:0;border-radius:6px;"
  "background:#0a7cff;color:#fff}.err{color:#c00}.ok{color:#080}.opt{font-size:13px;margin-top:8px}"
  "a{color:#0a7cff}"
  "</style></head><body><div class='card'>";

static const char PAGE_TAIL[] PROGMEM = "</div></body></html>";

static const char PAGE_FORM[] PROGMEM =
  "<h1>Configuration WiFi</h1>"
  "<form method='POST' action='/save'>"
  "<label for='s'>Nom du réseau (SSID)</label>"
  "<input id='s' name='ssid' type='text' maxlength='32' required "
  "autocapitalize='none' autocomplete='off'>"
  "<label for='p'>Mot de passe</label>"
  "<input id='p' name='password' type='password' maxlength='63' autocomplete='off'>"
  "<div class='opt'><input id='c' type='checkbox' "
  "onclick=\"document.getElementById('p').type=this.checked?'text':'password'\">"
  " <label for='c' style='display:inline'>Afficher le mot de passe</label></div>"
  "<button type='submit'>Enregistrer</button></form>";

// Le SSID/topic/etc. vient de l'utilisateur : on l'échappe avant de l'insérer dans une page
static String htmlEscape(const String& in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    switch (in[i]) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += in[i];
    }
  }
  return out;
}

WifiAuto* WifiAuto::_instance = nullptr;

WifiAuto::WifiAuto(uint8_t buttonPin, const char* apName, const char* mqttStatusTopic)
  : _buttonPin(buttonPin), _apName(apName), _server(80),
    _mqttClient(_mqttWifiClient), _mqttStatusTopic(mqttStatusTopic ? mqttStatusTopic : "")
{
  // Une seule instance de WifiAuto peut utiliser MQTT : PubSubClient n'accepte pas de
  // callback membre, seulement une fonction statique (voir _staticMqttCallback)
  _instance = this;

  _mqttClient.setCallback(_staticMqttCallback);
  _mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
}

// ---------------------------------------------------------------------------
// Réglages WiFi
// ---------------------------------------------------------------------------

void WifiAuto::setButton(uint8_t pin, bool activeLow, bool usePullUp) {
  _buttonPin       = pin;
  _buttonActiveLow = activeLow;
  _buttonPullUp    = usePullUp;
}

void WifiAuto::setResetHoldTime(uint32_t ms)      { _resetHoldMs = ms; }
void WifiAuto::setReconnectInterval(uint32_t ms)  { _reconnectMs = ms; }
void WifiAuto::setRetryPolicy(uint8_t retries, uint32_t attemptTimeoutMs) {
  _maxRetries = retries;
  _attemptTimeoutMs = attemptTimeoutMs;
}
void WifiAuto::setDebug(bool enabled) { _debug = enabled; }
void WifiAuto::setPortalIP(IPAddress ip) { _apIP = ip; }
void WifiAuto::setHostname(const char* hostname)  { _hostname = hostname; }
void WifiAuto::onConnected(Callback callback)     { _onConnected = callback; }
void WifiAuto::onDisconnected(Callback callback)  { _onDisconnected = callback; }

// ---------------------------------------------------------------------------
// Réglages MQTT
// ---------------------------------------------------------------------------

void WifiAuto::setMqttCredentials(const char* username, const char* password) {
  _mqttUsername = username ? username : "";
  _mqttPassword = password ? password : "";
}

void WifiAuto::setMqttLastWill(const char* topic, const char* message, bool retain, uint8_t qos) {
  _lwtTopic   = topic ? topic : "";
  _lwtMessage = message ? message : "";
  _lwtRetain  = retain;
  _lwtQos     = qos;
}

void WifiAuto::setMqttMessageCallback(MqttMessageCallback callback) { _mqttMessageCallback = callback; }
void WifiAuto::setMqttReconnectInterval(uint32_t ms) { _mqttReconnectMs = ms; }
void WifiAuto::setMqttStatusInterval(uint32_t ms)    { _mqttStatusIntervalMs = ms; }
void WifiAuto::setMqttMaxRetries(uint8_t retries)    { _mqttMaxRetries = retries; }
void WifiAuto::onMqttConnected(Callback callback)    { _onMqttConnected = callback; }
void WifiAuto::onMqttDisconnected(Callback callback) { _onMqttDisconnected = callback; }

// ---------------------------------------------------------------------------
// Cycle de vie
// ---------------------------------------------------------------------------

void WifiAuto::begin() {
  if (_buttonActiveLow && _buttonPullUp) {
    pinMode(_buttonPin, INPUT_PULLUP);
  } else {
    pinMode(_buttonPin, INPUT);
  }

  // Les identifiants sont gérés par cette librairie : on évite que le driver WiFi
  // réécrive lui-même en flash à chaque WiFi.begin()
  WiFi.persistent(false);

  if (!_eventRegistered) {
    WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t info) {
      onWifiEvent(event, info);
    });
    _eventRegistered = true;
  }

  loadCredentials();
  loadMqttConfig();

  // Les routes HTTP sont enregistrées une seule fois ; seul le mode (AP ou station)
  // change entre startPortal() et startStation(), qui appellent chacun _server.begin()
  _server.on("/", HTTP_GET, [this]() { handleRoot(); });
  _server.on("/save", HTTP_POST, [this]() { handleSave(); });
  _server.on("/config", HTTP_GET, [this]() { handleConfig(); });
  _server.on("/config", HTTP_POST, [this]() { handleConfigSave(); });
  _server.on("/config/clear", HTTP_POST, [this]() { handleConfigClear(); });
  // Doit être enregistrée en dernier : ce joker (n'importe quelle URI/méthode) ne capture
  // que ce qu'aucune route précédente n'a matché (ex. requêtes de détection de portail
  // captif émises par les téléphones : /generate_204, /hotspot-detect.html, /ncsi.txt...).
  // Sans elle, ces requêtes ne matchent aucun handler et WebServer logue lui-même
  // "request handler not found" (niveau E) avant de retomber sur onNotFound() ci-dessous ;
  // en fournissant un handler qui matche toujours, ce log interne n'apparaît plus.
  _server.on(UriGlob("*"), HTTP_ANY, [this]() { handleNotFound(); });
  _server.onNotFound([this]() { handleNotFound(); });  // filet de sécurité, ne devrait plus être atteint

  if (_ssid.length() > 0) {
    startStation();
  } else {
    startPortal();
  }
}

void WifiAuto::update() {
  handleButton();

  if (_restartPending && (int32_t)(millis() - _restartAtMs) >= 0) {
    debugPrintf("Redemarrage");
    ESP.restart();
  }

  if (_portalActive) {
    _dns.processNextRequest();
  }
  _server.handleClient();

  if (!_portalActive) {
    handleStation();
    handleMqtt();
  }
}

void WifiAuto::clearCredentials() {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.clear();
  _prefs.end();
  _ssid = "";
  _password = "";
  debugPrintf("Identifiants effaces");
}

void WifiAuto::resetAndRestart() {
  clearCredentials();
  debugPrintf("Redemarrage en mode point d'acces");
  delay(100);
  ESP.restart();
}

bool WifiAuto::isConnected() const     { return WiFi.status() == WL_CONNECTED; }
bool WifiAuto::isPortalActive() const  { return _portalActive; }
bool WifiAuto::hasCredentials() const  { return _ssid.length() > 0; }
String WifiAuto::getSSID() const       { return _ssid; }

IPAddress WifiAuto::getLocalIP() const {
  return _portalActive ? WiFi.softAPIP() : WiFi.localIP();
}

uint8_t WifiAuto::getLastDisconnectReason() const {
  return _lastDisconnectReason;
}

String WifiAuto::getLastDisconnectReasonText() const {
  return disconnectReasonText(_lastDisconnectReason);
}

// ---------------------------------------------------------------------------
// Logs et événements WiFi
// ---------------------------------------------------------------------------

// Écrit une ligne sur le port série (préfixe [WifiAuto]) si le debug est activé
void WifiAuto::debugPrintf(const char* format, ...) const {
  if (!_debug) return;

  char buffer[192];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  Serial.print("[WifiAuto] ");
  Serial.println(buffer);
}

// Codes de motif de déconnexion (wifi_err_reason_t de l'ESP-IDF)
const char* WifiAuto::disconnectReasonText(uint8_t reason) {
  switch (reason) {
    case 0:   return "aucun";
    case 1:   return "motif non specifie";
    case 2:   return "authentification expiree";
    case 3:   return "deconnecte par le point d'acces";
    case 4:   return "association expiree";
    case 5:   return "point d'acces sature";
    case 8:   return "deconnexion volontaire";
    case 15:  return "echange de cles echoue (mot de passe incorrect probable)";
    case 16:  return "mise a jour de la cle de groupe expiree";
    case 200: return "signal perdu (beacon timeout)";
    case 201: return "reseau introuvable";
    case 202: return "authentification refusee (mot de passe incorrect probable)";
    case 203: return "association refusee";
    case 204: return "delai de negociation depasse (mot de passe incorrect probable)";
    case 205: return "connexion echouee";
    default:  return "motif inconnu";
  }
}

// Appelée par le driver WiFi (dans sa propre tâche) à chaque événement
void WifiAuto::onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      debugPrintf("[evt] WiFi station demarre");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      debugPrintf("[evt] Associe au point d'acces (canal %u)", info.wifi_sta_connected.channel);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      debugPrintf("[evt] Adresse IP obtenue : %s", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      _lastDisconnectReason = info.wifi_sta_disconnected.reason;
      debugPrintf("[evt] Deconnecte, motif %u : %s", _lastDisconnectReason,
                  disconnectReasonText(_lastDisconnectReason));
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      const uint8_t* m = info.wifi_ap_staconnected.mac;
      debugPrintf("[evt] Client connecte au portail : %02X:%02X:%02X:%02X:%02X:%02X",
                  m[0], m[1], m[2], m[3], m[4], m[5]);
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      const uint8_t* m = info.wifi_ap_stadisconnected.mac;
      debugPrintf("[evt] Client deconnecte du portail : %02X:%02X:%02X:%02X:%02X:%02X",
                  m[0], m[1], m[2], m[3], m[4], m[5]);
      break;
    }
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Mémoire — WiFi
// ---------------------------------------------------------------------------

void WifiAuto::loadCredentials() {
  // Ouverture en lecture/écriture : en lecture seule, l'espace de noms doit déjà exister
  // (erreur NOT_FOUND au tout premier démarrage)
  _prefs.begin(NVS_NAMESPACE, false);
  // isKey() évite les erreurs "NOT_FOUND" affichées par getString() quand la clé n'existe pas
  _ssid     = _prefs.isKey(NVS_KEY_SSID) ? _prefs.getString(NVS_KEY_SSID, "") : "";
  _password = _prefs.isKey(NVS_KEY_PASS) ? _prefs.getString(NVS_KEY_PASS, "") : "";
  _unverified = _prefs.getBool(NVS_KEY_UNVERIFIED, false);
  _prefs.end();
}

// Les nouveaux identifiants sont marqués "non validés" tant qu'aucune connexion n'a réussi
void WifiAuto::saveCredentials(const String& ssid, const String& password) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString(NVS_KEY_SSID, ssid);
  _prefs.putString(NVS_KEY_PASS, password);
  _prefs.putBool(NVS_KEY_UNVERIFIED, true);
  _prefs.end();
}

void WifiAuto::markVerified() {
  _unverified = false;
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putBool(NVS_KEY_UNVERIFIED, false);
  _prefs.end();
}

// ---------------------------------------------------------------------------
// Modes de fonctionnement WiFi
// ---------------------------------------------------------------------------

void WifiAuto::startStation() {
  _portalActive = false;

  // Le nom d'hôte doit être défini avant WiFi.mode()
  if (_hostname.length() > 0) {
    WiFi.setHostname(_hostname.c_str());
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  debugPrintf("Connexion a %s", _ssid.c_str());
  WiFi.begin(_ssid.c_str(), _password.c_str());
  _lastAttemptMs = millis();
  _retryCount = 0;

  _server.begin();
}

// Le réseau saisi est injoignable (mot de passe faux ?) : on l'oublie et on rouvre le portail
void WifiAuto::fallbackToPortal() {
  debugPrintf("Echec de connexion a %s, retour au portail de configuration", _ssid.c_str());

  _failedSsid = _ssid;
  clearCredentials();
  WiFi.disconnect();
  _wasConnected = false;
  startPortal();
}

void WifiAuto::startPortal() {
  _portalActive = true;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(_apIP, _apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(_apName.c_str());  // réseau ouvert : pas de mot de passe

  // Toute résolution DNS pointe vers l'ESP32 : le portail s'ouvre automatiquement
  _dns.start(53, "*", WiFi.softAPIP());

  _server.begin();

  debugPrintf("Point d'acces \"%s\" actif - page de configuration : http://%s",
              _apName.c_str(), WiFi.softAPIP().toString().c_str());
}

// ---------------------------------------------------------------------------
// Bouton : appui long -> effacement + redémarrage
// ---------------------------------------------------------------------------

void WifiAuto::handleButton() {
  bool pressed = (digitalRead(_buttonPin) == (_buttonActiveLow ? LOW : HIGH));

  if (!pressed) {
    _pressed = false;
    return;
  }

  if (!_pressed) {
    _pressed = true;
    _pressStartMs = millis();
    return;
  }

  if (millis() - _pressStartMs >= _resetHoldMs) {
    resetAndRestart();
  }
}

// ---------------------------------------------------------------------------
// Mode station : suivi de l'état et reconnexion automatique
// ---------------------------------------------------------------------------

void WifiAuto::handleStation() {
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected && !_wasConnected) {
    _wasConnected = true;
    if (_unverified) markVerified();
    debugPrintf("Connecte, IP : %s", WiFi.localIP().toString().c_str());
    if (_onConnected) _onConnected();
    return;
  }

  if (!connected && _wasConnected) {
    _wasConnected = false;
    _lastAttemptMs = millis();
    debugPrintf("Connexion perdue (motif : %s)", disconnectReasonText(_lastDisconnectReason));
    if (_onDisconnected) _onDisconnected();
    return;
  }

  if (connected) return;

  // Réseau jamais validé : durée limitée, puis retour au portail
  if (_unverified) {
    if (millis() - _lastAttemptMs < _attemptTimeoutMs) return;
    if (_retryCount >= _maxRetries) {
      fallbackToPortal();
      return;
    }
    _retryCount++;
    debugPrintf("Nouvelle tentative %u/%u", _retryCount, _maxRetries);
    WiFi.disconnect();
    WiFi.begin(_ssid.c_str(), _password.c_str());
    _lastAttemptMs = millis();
    return;
  }

  // Réseau déjà validé : filet de sécurité en plus de setAutoReconnect(), on relance
  // périodiquement la connexion tant que le réseau est injoignable
  if (millis() - _lastAttemptMs >= _reconnectMs) {
    debugPrintf("Tentative de reconnexion...");
    WiFi.disconnect();
    WiFi.begin(_ssid.c_str(), _password.c_str());
    _lastAttemptMs = millis();
  }
}

// ---------------------------------------------------------------------------
// Mémoire — MQTT
// ---------------------------------------------------------------------------

void WifiAuto::loadMqttConfig() {
  _prefs.begin(NVS_MQTT_NAMESPACE, false);
  _mqttHost     = _prefs.isKey(MQTT_KEY_HOST) ? _prefs.getString(MQTT_KEY_HOST, "") : "";
  _mqttPort     = _prefs.isKey(MQTT_KEY_PORT) ? _prefs.getUShort(MQTT_KEY_PORT, 1883) : 1883;
  _mqttClientId = _prefs.isKey(MQTT_KEY_CID)  ? _prefs.getString(MQTT_KEY_CID, "")  : "";
  // Un identifiant/mot de passe déjà réglé par setMqttCredentials() n'est écrasé que si
  // la mémoire contient une valeur (permet de définir un défaut avant begin())
  if (_prefs.isKey(MQTT_KEY_USER)) _mqttUsername = _prefs.getString(MQTT_KEY_USER, "");
  if (_prefs.isKey(MQTT_KEY_PASS)) _mqttPassword = _prefs.getString(MQTT_KEY_PASS, "");
  _mqttVerified = _prefs.getBool(MQTT_KEY_VERIFIED, false);
  _prefs.end();

  if (hasMqttConfig()) {
    _mqttClient.setServer(_mqttHost.c_str(), _mqttPort);
  }
}

// Une configuration tout juste saisie est enregistrée comme "non validée" : on ne sait pas
// encore si elle est correcte tant qu'une connexion n'a pas réussi (voir mqttConnectAttempt())
void WifiAuto::saveMqttConfig(const String& host, uint16_t port, const String& clientId,
                               const String& username, const String& password) {
  _prefs.begin(NVS_MQTT_NAMESPACE, false);
  _prefs.putString(MQTT_KEY_HOST, host);
  _prefs.putUShort(MQTT_KEY_PORT, port);
  _prefs.putString(MQTT_KEY_CID, clientId);
  _prefs.putString(MQTT_KEY_USER, username);
  _prefs.putString(MQTT_KEY_PASS, password);
  _prefs.putBool(MQTT_KEY_VERIFIED, false);
  _prefs.end();

  _mqttHost     = host;
  _mqttPort     = port;
  _mqttClientId = clientId;
  _mqttUsername = username;
  _mqttPassword = password;
  _mqttVerified = false;
  _mqttFailCount = 0;
}

void WifiAuto::clearMqttConfig() {
  // Avant de couper la connexion, on publie nous-mêmes "offline" (retenu) sur le topic de
  // statut : sinon le broker ne l'affiche que via le testament, déclenché par le driver MQTT
  // après un délai (keep-alive), ce qui laisserait "online" affiché à tort entre-temps
  if (_mqttClient.connected() && _mqttStatusTopic.length() > 0) {
    bool ok = _mqttClient.publish(_mqttStatusTopic.c_str(), "offline", true);
    debugPrintf("[MQTT] Statut publie sur '%s' : offline (retain) -> %s",
                _mqttStatusTopic.c_str(), ok ? "OK" : "ERREUR");
  }

  if (_mqttClient.connected()) _mqttClient.disconnect();
  _mqttWasConnected = false;
  _mqttVerified = false;
  _mqttFailCount = 0;

  _prefs.begin(NVS_MQTT_NAMESPACE, false);
  _prefs.clear();
  _prefs.end();

  _mqttHost = "";
  _mqttPort = 1883;
  _mqttClientId = "";
  _mqttUsername = "";
  _mqttPassword = "";

  debugPrintf("[MQTT] Configuration effacee");
}

// Applique la config MQTT courante au client et force une reconnexion au prochain update()
void WifiAuto::applyMqttConfig() {
  if (hasMqttConfig()) {
    _mqttClient.setServer(_mqttHost.c_str(), _mqttPort);
  }
  if (_mqttClient.connected()) _mqttClient.disconnect();
  _mqttWasConnected = false;
  _lastMqttAttemptMs = 0;  // force une tentative dès le prochain update()
}

// La configuration vient de se connecter avec succès pour la première fois : elle est
// désormais considérée comme correcte et ne sera plus jamais effacée automatiquement
void WifiAuto::markMqttVerified() {
  _mqttVerified = true;
  _mqttFailCount = 0;
  _prefs.begin(NVS_MQTT_NAMESPACE, false);
  _prefs.putBool(MQTT_KEY_VERIFIED, true);
  _prefs.end();
}

// ---------------------------------------------------------------------------
// MQTT : connexion, reconnexion, statut
// ---------------------------------------------------------------------------

bool WifiAuto::hasMqttConfig() const { return _mqttHost.length() > 0; }
String WifiAuto::getMqttHost() const { return _mqttHost; }
uint16_t WifiAuto::getMqttPort() const { return _mqttPort; }
bool WifiAuto::isMqttConnected() { return _mqttClient.connected(); }
int WifiAuto::getMqttStateCode() { return _mqttClient.state(); }
String WifiAuto::getMqttStateText() { return mqttStateTextFor(_mqttClient.state()); }

// Identifiant client par défaut si aucun n'est configuré, dérivé de l'adresse MAC
String WifiAuto::defaultMqttClientId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[20];
  snprintf(buf, sizeof(buf), "WifiAuto-%06X", (unsigned int)(mac & 0xFFFFFF));
  return String(buf);
}

const char* WifiAuto::mqttStateTextFor(int state) {
  switch (state) {
    case -4: return "delai de connexion depasse";
    case -3: return "connexion perdue";
    case -2: return "echec de connexion au serveur";
    case -1: return "deconnecte";
    case 0:  return "connecte";
    case 1:  return "protocole MQTT non supporte par le serveur";
    case 2:  return "identifiant client rejete par le serveur";
    case 3:  return "serveur MQTT indisponible";
    case 4:  return "identifiants incorrects";
    case 5:  return "non autorise";
    default: return "etat inconnu";
  }
}

void WifiAuto::handleMqtt() {
  if (!hasMqttConfig()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (_mqttClient.connected()) {
    _mqttClient.loop();
    if (_mqttStatusTopic.length() > 0 && millis() - _lastMqttStatusMs >= _mqttStatusIntervalMs) {
      publishMqttStatus();
    }
    return;
  }

  if (_mqttWasConnected) {
    _mqttWasConnected = false;
    debugPrintf("[MQTT] Connexion perdue (etat=%d : %s)",
                _mqttClient.state(), mqttStateTextFor(_mqttClient.state()));
    if (_onMqttDisconnected) _onMqttDisconnected();
  }

  if (millis() - _lastMqttAttemptMs < _mqttReconnectMs) return;
  _lastMqttAttemptMs = millis();
  mqttConnectAttempt();
}

void WifiAuto::mqttConnectAttempt() {
  String clientId = _mqttClientId.length() > 0 ? _mqttClientId : defaultMqttClientId();

  if (_mqttVerified) {
    debugPrintf("[MQTT] Connexion a %s:%u en tant que '%s'...",
                _mqttHost.c_str(), _mqttPort, clientId.c_str());
  } else {
    debugPrintf("[MQTT] Connexion a %s:%u en tant que '%s'... (tentative %u/%u)",
                _mqttHost.c_str(), _mqttPort, clientId.c_str(), _mqttFailCount + 1, _mqttMaxRetries);
  }

  const char* user = _mqttUsername.length() > 0 ? _mqttUsername.c_str() : nullptr;
  const char* pass = _mqttPassword.length() > 0 ? _mqttPassword.c_str() : nullptr;

  // Testament : celui défini par setMqttLastWill() prime sur le testament automatique
  // ("offline", retenu) construit à partir du topic de statut du constructeur
  bool customLwt = _lwtTopic.length() > 0;
  const char* willTopic   = customLwt ? _lwtTopic.c_str()
                          : (_mqttStatusTopic.length() > 0 ? _mqttStatusTopic.c_str() : nullptr);
  const char* willMessage = customLwt ? _lwtMessage.c_str() : "offline";
  bool        willRetain  = customLwt ? _lwtRetain : true;
  uint8_t     willQos     = customLwt ? _lwtQos : 1;

  bool ok;
  if (willTopic) {
    ok = user ? _mqttClient.connect(clientId.c_str(), user, pass, willTopic, willQos, willRetain, willMessage)
              : _mqttClient.connect(clientId.c_str(), nullptr, nullptr, willTopic, willQos, willRetain, willMessage);
  } else {
    ok = user ? _mqttClient.connect(clientId.c_str(), user, pass)
              : _mqttClient.connect(clientId.c_str());
  }

  if (ok) {
    debugPrintf("[MQTT] Connecte.");
    _mqttWasConnected = true;
    if (!_mqttVerified) markMqttVerified();
    publishMqttStatus();
    if (_onMqttConnected) _onMqttConnected();
    return;
  }

  // Configuration déjà validée par le passé : un échec ponctuel n'est qu'une coupure
  // temporaire (broker éteint, réseau...), on continue de réessayer indéfiniment
  if (_mqttVerified) {
    debugPrintf("[MQTT] Echec (etat=%d : %s). Nouvelle tentative dans %u ms...",
                _mqttClient.state(), mqttStateTextFor(_mqttClient.state()), _mqttReconnectMs);
    return;
  }

  // Configuration jamais validée : après plusieurs échecs, elle est probablement incorrecte
  // (mauvaise adresse, mauvais identifiants...) plutôt que temporairement indisponible
  _mqttFailCount++;
  debugPrintf("[MQTT] Echec (etat=%d : %s), tentative %u/%u",
              _mqttClient.state(), mqttStateTextFor(_mqttClient.state()), _mqttFailCount, _mqttMaxRetries);

  if (_mqttFailCount >= _mqttMaxRetries) {
    debugPrintf("[MQTT] Echec apres %u tentatives : la configuration semble incorrecte, "
                "suppression de la configuration MQTT.", _mqttFailCount);
    clearMqttConfig();
  }
}

// Publie l'état "online" (retenu) sur le topic de statut, s'il est configuré
void WifiAuto::publishMqttStatus() {
  if (_mqttStatusTopic.length() == 0 || !_mqttClient.connected()) return;

  bool ok = _mqttClient.publish(_mqttStatusTopic.c_str(), "online", true);
  debugPrintf("[MQTT] Statut publie sur '%s' : online (retain) -> %s",
              _mqttStatusTopic.c_str(), ok ? "OK" : "ERREUR");
  _lastMqttStatusMs = millis();
}

bool WifiAuto::mqttPublish(const char* topic, const String& payload, bool retain, uint8_t qos) {
  return mqttPublish(topic, payload.c_str(), retain, qos);
}

bool WifiAuto::mqttPublish(const char* topic, const char* payload, bool retain, uint8_t qos) {
  (void)qos;  // PubSubClient ne publie qu'en QoS 0 ; conservé pour compatibilité d'API
  // Pas de log ici : appeler publish() alors que le broker n'est pas encore connecté est un
  // cas normal (ex. publication périodique dans loop()), pas une anomalie à tracer
  if (!_mqttClient.connected()) return false;

  bool ok = _mqttClient.publish(topic, payload, retain);
  debugPrintf("[MQTT] publish('%s', '%s', retain=%d) -> %s", topic, payload, retain, ok ? "OK" : "ERREUR");
  return ok;
}

bool WifiAuto::mqttSubscribe(const char* topic, uint8_t qos) {
  // Pas de log ici : appeler subscribe() alors que le broker n'est pas encore connecté est un
  // cas normal, pas une anomalie à tracer
  if (!_mqttClient.connected()) return false;

  bool ok = _mqttClient.subscribe(topic, qos);
  debugPrintf("[MQTT] subscribe('%s', qos=%u) -> %s", topic, qos, ok ? "OK" : "ERREUR");
  return ok;
}

bool WifiAuto::mqttUnsubscribe(const char* topic) {
  if (!_mqttClient.connected()) return false;
  bool ok = _mqttClient.unsubscribe(topic);
  debugPrintf("[MQTT] unsubscribe('%s') -> %s", topic, ok ? "OK" : "ERREUR");
  return ok;
}

// Adaptateur statique -> instance : PubSubClient n'accepte pas de callback membre
void WifiAuto::_staticMqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!_instance || !_instance->_mqttMessageCallback) return;

  String topicStr(topic);
  String payloadStr;
  payloadStr.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    payloadStr += static_cast<char>(payload[i]);
  }

  _instance->_mqttMessageCallback(topicStr, payloadStr);
}

// ---------------------------------------------------------------------------
// Pages web — portail WiFi (mode point d'accès uniquement)
// ---------------------------------------------------------------------------

void WifiAuto::handleRoot() {
  String html = FPSTR(PAGE_HEAD);

  if (_portalActive) {
    if (_failedSsid.length() > 0) {
      html += "<p class='err'>Connexion impossible au réseau <b>" + htmlEscape(_failedSsid) +
              "</b>. Vérifiez le nom et le mot de passe puis réessayez.</p>";
    }
    html += FPSTR(PAGE_FORM);
  } else {
    // En mode station, la racine sert de point d'entrée vers la configuration MQTT
    html += "<h1>ESP32 connecté</h1><p>Réseau WiFi : <b>" + htmlEscape(_ssid) + "</b><br>"
            "IP : " + WiFi.localIP().toString() + "</p>"
            "<p><a href='/config'>Configuration MQTT</a></p>";
  }

  html += FPSTR(PAGE_TAIL);
  _server.send(200, "text/html; charset=utf-8", html);
}

void WifiAuto::handleSave() {
  String ssid     = _server.arg("ssid");
  String password = _server.arg("password");

  String error;
  if (ssid.length() == 0 || ssid.length() > 32) {
    error = "Le SSID doit contenir entre 1 et 32 caractères.";
  } else if (password.length() > 0 && (password.length() < 8 || password.length() > 63)) {
    error = "Le mot de passe doit contenir entre 8 et 63 caractères (ou être vide pour un réseau ouvert).";
  }

  String html = FPSTR(PAGE_HEAD);
  if (error.length() > 0) {
    html += "<h1>Erreur</h1><p class='err'>" + error + "</p><p><a href='/'>Retour</a></p>";
    html += FPSTR(PAGE_TAIL);
    _server.send(400, "text/html; charset=utf-8", html);
    return;
  }

  saveCredentials(ssid, password);
  debugPrintf("Identifiants enregistres pour %s", ssid.c_str());

  html += "<h1>Enregistré</h1><p>L'ESP32 redémarre et va se connecter au réseau "
          "<b>WiFi choisi</b>. Vous pouvez fermer cette page.</p>";
  html += FPSTR(PAGE_TAIL);
  _server.send(200, "text/html; charset=utf-8", html);

  _restartPending = true;
  _restartAtMs = millis() + RESTART_DELAY_MS;
}

// Redirige toute URL inconnue en mode point d'accès (tests de connectivité des téléphones,
// etc.) vers le portail. En mode station, réponse 404 classique.
void WifiAuto::handleNotFound() {
  if (_portalActive) {
    _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    _server.send(302, "text/plain", "");
  } else {
    _server.send(404, "text/plain", "Not Found");
  }
}

// ---------------------------------------------------------------------------
// Pages web — configuration MQTT (mode station uniquement)
// ---------------------------------------------------------------------------

void WifiAuto::handleConfig() {
  // Cette page n'a de sens que connecté à un réseau WiFi existant
  if (_portalActive) {
    _server.send(404, "text/plain", "Page indisponible en mode point d'acces");
    return;
  }

  String html = FPSTR(PAGE_HEAD);
  html += "<h1>Configuration MQTT</h1>";
  html += "<p>Réseau WiFi : <b>" + htmlEscape(_ssid) + "</b>, IP : " + WiFi.localIP().toString() + "</p>";

  if (!hasMqttConfig()) {
    html += "<p>Aucune configuration MQTT enregistrée.</p>";
  } else if (isMqttConnected()) {
    html += "<p class='ok'>Connecté au broker MQTT à " + htmlEscape(_mqttHost) + ":" +
            String(_mqttPort) + "</p>";
  } else {
    html += "<p class='err'>Non connecté au broker MQTT à " + htmlEscape(_mqttHost) + ":" +
            String(_mqttPort) + " (" + getMqttStateText() + ")</p>";
    if (!_mqttVerified) {
      html += "<p class='opt'>Configuration pas encore validée : tentative " +
              String(_mqttFailCount) + "/" + String(_mqttMaxRetries) +
              ". Au-delà, la configuration sera effacée automatiquement.</p>";
    }
  }

  html += "<form method='POST' action='/config'>"
          "<label for='h'>Adresse du broker (IP ou nom)</label>"
          "<input id='h' name='host' type='text' maxlength='128' required value='" +
          htmlEscape(_mqttHost) + "'>"
          "<label for='po'>Port</label>"
          "<input id='po' name='port' type='number' min='1' max='65535' value='" +
          String(_mqttPort) + "'>"
          "<label for='ci'>Identifiant client (optionnel)</label>"
          "<input id='ci' name='clientid' type='text' maxlength='64' value='" +
          htmlEscape(_mqttClientId) + "'>"
          "<label for='us'>Utilisateur (optionnel)</label>"
          "<input id='us' name='username' type='text' maxlength='64' value='" +
          htmlEscape(_mqttUsername) + "' autocomplete='off'>"
          "<label for='pw'>Mot de passe (optionnel)</label>"
          "<input id='pw' name='password' type='password' maxlength='64' autocomplete='off'>"
          "<div class='opt'>Laisser vide pour conserver le mot de passe enregistré.</div>"
          "<button type='submit'>Enregistrer</button></form>";

  if (hasMqttConfig()) {
    html += "<form method='POST' action='/config/clear' "
            "onsubmit=\"return confirm('Effacer la configuration MQTT ?');\">"
            "<button type='submit' style='background:#c00;margin-top:10px'>"
            "Effacer la configuration MQTT</button></form>";
  }

  html += "<p class='opt'><a href='/'>Retour</a></p>";
  html += FPSTR(PAGE_TAIL);
  _server.send(200, "text/html; charset=utf-8", html);
}

void WifiAuto::handleConfigSave() {
  if (_portalActive) {
    _server.send(404, "text/plain", "Page indisponible en mode point d'acces");
    return;
  }

  String host     = _server.arg("host");
  String portStr  = _server.arg("port");
  String clientId = _server.arg("clientid");
  String username = _server.arg("username");
  String password = _server.arg("password");

  host.trim();
  clientId.trim();
  username.trim();

  long port = portStr.length() > 0 ? portStr.toInt() : 1883;

  String error;
  if (host.length() == 0 || host.length() > 128) {
    error = "L'adresse du broker doit contenir entre 1 et 128 caractères.";
  } else if (port < 1 || port > 65535) {
    error = "Le port doit être compris entre 1 et 65535.";
  } else if (clientId.length() > 64 || username.length() > 64 || password.length() > 64) {
    error = "Un des champs dépasse la longueur maximale autorisée (64 caractères).";
  }

  if (error.length() > 0) {
    String html = FPSTR(PAGE_HEAD);
    html += "<h1>Erreur</h1><p class='err'>" + error + "</p><p><a href='/config'>Retour</a></p>";
    html += FPSTR(PAGE_TAIL);
    _server.send(400, "text/html; charset=utf-8", html);
    return;
  }

  // Champ mot de passe laissé vide : on conserve le mot de passe déjà enregistré
  if (password.length() == 0) password = _mqttPassword;

  saveMqttConfig(host, (uint16_t)port, clientId, username, password);
  applyMqttConfig();

  debugPrintf("[MQTT] Configuration enregistree : %s:%u", host.c_str(), (uint16_t)port);

  String html = FPSTR(PAGE_HEAD);
  html += "<h1>Enregistré</h1><p>Connexion au broker MQTT en cours...</p>"
          "<p><a href='/config'>Voir le statut</a></p>";
  html += FPSTR(PAGE_TAIL);
  _server.send(200, "text/html; charset=utf-8", html);
}

void WifiAuto::handleConfigClear() {
  if (_portalActive) {
    _server.send(404, "text/plain", "Page indisponible en mode point d'acces");
    return;
  }

  clearMqttConfig();

  String html = FPSTR(PAGE_HEAD);
  html += "<h1>Configuration MQTT effacée</h1><p><a href='/config'>Retour</a></p>";
  html += FPSTR(PAGE_TAIL);
  _server.send(200, "text/html; charset=utf-8", html);
}
