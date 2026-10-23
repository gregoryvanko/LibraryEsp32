#include "ESPConnect.h"

#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

#include "mbedtls/ssl.h"

#ifdef ESPCONNECT_NO_LOG
#define ESPC_LOG(...) \
  do {                \
  } while (0)
#else
#define ESPC_LOG(fmt, ...) espcLog("[ESPConnect] " fmt, ##__VA_ARGS__)
#endif

ESPConnect espConnect;

namespace {

#ifndef ESPCONNECT_NO_LOG
// Lettre latine accentuée (second octet d'une séquence UTF-8 0xC3 xx) vers sa lettre de base.
char unaccent(uint8_t b) {
  uint8_t cp = b + 0x40;  // point de code Latin-1
  if (cp >= 0xC0 && cp <= 0xC5) return 'A';
  if (cp == 0xC7) return 'C';
  if (cp >= 0xC8 && cp <= 0xCB) return 'E';
  if (cp >= 0xCC && cp <= 0xCF) return 'I';
  if (cp == 0xD1) return 'N';
  if (cp >= 0xD2 && cp <= 0xD6) return 'O';
  if (cp >= 0xD9 && cp <= 0xDC) return 'U';
  if (cp >= 0xE0 && cp <= 0xE5) return 'a';
  if (cp == 0xE7) return 'c';
  if (cp >= 0xE8 && cp <= 0xEB) return 'e';
  if (cp >= 0xEC && cp <= 0xEF) return 'i';
  if (cp == 0xF1) return 'n';
  if (cp >= 0xF2 && cp <= 0xF6) return 'o';
  if (cp >= 0xF9 && cp <= 0xFC) return 'u';
  if (cp == 0xFD || cp == 0xFF) return 'y';
  return '?';
}

// Le journal est écrit en ASCII pur : les caractères UTF-8 sur plusieurs octets
// (accents, « », —, …) s'affichent en "?" dans certains moniteurs série.
void espcLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void espcLog(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  String out;
  out.reserve(strlen(buf) + 8);
  const uint8_t *p = reinterpret_cast<const uint8_t *>(buf);
  while (*p) {
    if (*p < 0x80) {
      out += static_cast<char>(*p++);
    } else if (p[0] == 0xC3 && p[1]) {
      out += unaccent(p[1]);
      p += 2;
    } else if (p[0] == 0xC2 && p[1] == 0xAB) {  // « suivi d'une espace
      out += '"';
      p += 2;
      if (*p == ' ') p++;
    } else if (p[0] == 0xC2 && p[1] == 0xBB) {  // espace suivie de »
      if (out.length() && out[out.length() - 1] == ' ') out.remove(out.length() - 1);
      out += '"';
      p += 2;
    } else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x93 || p[2] == 0x94)) {
      out += '-';
      p += 3;
    } else if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0xA6) {
      out += "...";
      p += 3;
    } else {
      // Autre caractère : remplacé par '?' en sautant toute sa séquence.
      size_t n = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 1;
      out += '?';
      while (n-- && *p) p++;
    }
  }
  out += '\n';
  Serial.print(out);
}
#endif

const char *NVS_WIFI = "espc_wifi";
const char *NVS_MQTT = "espc_mqtt";
const char *NVS_DEVICE = "espc_dev";

const IPAddress AP_IP(ESPCONNECT_AP_IP);
const IPAddress AP_MASK(255, 255, 255, 0);

// Verrou RAII sur le mutex partagé entre loop(), la tâche du serveur web et la tâche MQTT.
class Guard {
public:
  explicit Guard(SemaphoreHandle_t lock) : _lock(lock) {
    if (_lock) xSemaphoreTake(_lock, portMAX_DELAY);
  }
  ~Guard() {
    if (_lock) xSemaphoreGive(_lock);
  }

private:
  SemaphoreHandle_t _lock;
};

// TLS sans vérification de certificat : esp-tls applique VERIFY_REQUIRED puis appelle
// ce "bundle" personnalisé, qui repasse la vérification en mode NONE.
esp_err_t insecureTlsAttach(void *conf) {
  mbedtls_ssl_conf_authmode(static_cast<mbedtls_ssl_config *>(conf), MBEDTLS_SSL_VERIFY_NONE);
  return ESP_OK;
}

bool isValidName(const String &name) {
  if (name.isEmpty() || name.length() > 32) return false;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
  }
  return true;
}

bool isValidScheme(const String &s) { return s == "mqtt" || s == "mqtts" || s == "ws" || s == "wss"; }

uint16_t defaultPort(const String &scheme) {
  if (scheme == "mqtt") return 1883;
  if (scheme == "mqtts") return 8883;
  if (scheme == "ws") return 80;
  return 443;
}

const char *mqttStateName(ESPConnect::MqttState s) {
  switch (s) {
    case ESPConnect::MqttState::Connecting: return "connecting";
    case ESPConnect::MqttState::Connected: return "connected";
    case ESPConnect::MqttState::Error: return "error";
    default: return "disabled";
  }
}

String shorten(const String &s, size_t max = 80) { return s.length() <= max ? s : s.substring(0, max) + "…"; }

// Preferences::getString() journalise une erreur quand la clé n'existe pas encore :
// on vérifie sa présence avant de la lire.
String readString(Preferences &p, const char *key, const String &fallback = String()) {
  return p.isKey(key) ? p.getString(key, fallback) : fallback;
}

void eraseNamespace(const char *ns) {
  Preferences p;
  p.begin(ns, false);
  p.clear();
  p.end();
}

// ---------------------------------------------------------------------------
// Pages web
// ---------------------------------------------------------------------------

#define ESPC_HEAD                                                                                          \
  "<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\">"                                      \
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>ESPConnect</title><style>" \
  ":root{--bg:#f4f6f8;--card:#fff;--fg:#1d2733;--mut:#6b7785;--line:#dde3ea;--acc:#1f7a5a;--err:#c0392b}"   \
  "@media(prefers-color-scheme:dark){:root{--bg:#12171d;--card:#1b232c;--fg:#e6ebf0;--mut:#93a0ad;"         \
  "--line:#2c3742;--acc:#3fbf8f;--err:#ff6b5b}}"                                                           \
  "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);"                           \
  "font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}"                                    \
  "main{max-width:560px;margin:0 auto;padding:16px}h1{font-size:1.4em;margin:8px 0 16px}"                 \
  "h2{font-size:1.05em;margin:0 0 10px}section{background:var(--card);border:1px solid var(--line);"      \
  "border-radius:10px;padding:14px 16px;margin-bottom:14px}"                                              \
  "label{display:block;margin:10px 0 4px;font-weight:600}label.row{display:flex;gap:8px;"                 \
  "align-items:center;font-weight:400}"                                                                   \
  "input,select{width:100%;padding:9px 10px;margin-top:4px;border:1px solid var(--line);border-radius:6px;" \
  "background:var(--bg);color:var(--fg);font:inherit}input[type=checkbox]{width:auto;margin:0}"           \
  "button{width:100%;margin-top:12px;padding:10px;border:0;border-radius:6px;background:var(--acc);"      \
  "color:#fff;font:inherit;font-weight:600;cursor:pointer}button.sec{background:transparent;"             \
  "color:var(--acc);border:1px solid var(--line)}button.danger{background:var(--err)}"                    \
  ".muted{color:var(--mut);font-size:.9em}table{width:100%;border-collapse:collapse}"                     \
  "th,td{text-align:left;padding:5px 0;border-bottom:1px solid var(--line);vertical-align:top}"           \
  "th{font-weight:500;color:var(--mut);width:45%}td{word-break:break-all}"                                \
  ".net{display:flex;justify-content:space-between;margin:6px 0 0;background:var(--bg);color:var(--fg);"  \
  "border:1px solid var(--line);font-weight:400}"                                                         \
  ".dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:8px;background:#999}"  \
  ".dot.on{background:var(--acc)}.dot.wait{background:#e0a100}.dot.err{background:var(--err)}"           \
  "</style></head><body><main>"

const char PORTAL_HTML[] PROGMEM = ESPC_HEAD R"rawliteral(
<h1>Configuration WiFi</h1>
<section><h2>Réseaux détectés</h2>
<div id="nets"><p class="muted">Recherche des réseaux…</p></div>
<button type="button" class="sec" onclick="scan()">Actualiser</button></section>
<section><form method="post" action="/save">
<label>SSID<input name="ssid" id="ssid" maxlength="32" required></label>
<label>Mot de passe<input name="pass" id="pass" type="password" maxlength="63" placeholder="Vide pour un réseau ouvert"></label>
<label class="row"><input type="checkbox" onclick="document.getElementById('pass').type=this.checked?'text':'password'">Afficher le mot de passe</label>
<button type="submit">Enregistrer et redémarrer</button></form></section>
</main><script>
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
const bars=r=>r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂';
function scan(){$('nets').innerHTML='<p class="muted">Recherche des réseaux…</p>';poll();}
async function poll(){try{const r=await fetch('/scan');if(r.status==202){setTimeout(poll,1500);return;}show(await r.json());}catch(e){setTimeout(poll,2000);}}
function show(l){const d=$('nets');if(!l.length){d.innerHTML='<p class="muted">Aucun réseau trouvé.</p>';return;}
d.innerHTML='';l.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{const b=document.createElement('button');b.type='button';b.className='net';
b.innerHTML='<span>'+esc(n.ssid)+'</span><span>'+(n.secure?'🔒 ':'')+bars(n.rssi)+'</span>';
b.onclick=()=>{$('ssid').value=n.ssid;$('pass').focus();};d.appendChild(b);});}
poll();
</script></body></html>)rawliteral";

const char SAVED_HTML[] PROGMEM = ESPC_HEAD R"rawliteral(
<h1>Identifiants enregistrés</h1>
<section><p>L'ESP32 redémarre et va se connecter au réseau choisi.</p>
<p class="muted">Reconnectez votre appareil à votre réseau habituel. Si la connexion échoue,
le point d'accès de configuration réapparaîtra après quelques tentatives.</p></section>
</main></body></html>)rawliteral";

const char STATUS_HTML[] PROGMEM = ESPC_HEAD R"rawliteral(
<h1>ESPConnect · <span id="name"></span></h1>
<section><h2>WiFi</h2><table id="wifi"></table></section>
<section><h2>MQTT</h2><p><span id="mq-dot" class="dot"></span><b id="mq-state">…</b></p><table id="mqtt"></table></section>
<section><h2>Configuration</h2><form id="f">
<label>Nom de l'appareil<input name="name" id="f-name" maxlength="32" pattern="[A-Za-z0-9_\-]+" required></label>
<label class="row"><input type="checkbox" id="f-en">Activer la connexion MQTT</label>
<div id="mq-fields" hidden>
<label>Protocole<select name="scheme" id="f-scheme">
<option value="mqtt">mqtt:// (TCP)</option><option value="mqtts">mqtts:// (TLS)</option>
<option value="ws">ws:// (WebSocket)</option><option value="wss">wss:// (WebSocket TLS)</option></select></label>
<label>Serveur<input name="host" id="f-host" placeholder="broker.example.com"></label>
<label>Port<input name="port" id="f-port" type="number" min="1" max="65535"></label>
<label id="l-path">Chemin WebSocket<input name="path" id="f-path" placeholder="/mqtt"></label>
<label>Utilisateur<input name="user" id="f-user" autocomplete="off"></label>
<label>Mot de passe<input name="pass" id="f-pass" type="password" autocomplete="new-password"></label>
<p class="muted">En TLS (mqtts, wss), le certificat du serveur n'est pas vérifié.</p>
</div>
<button type="submit">Enregistrer et appliquer</button>
<p id="msg" class="muted"></p></form>
<button type="button" class="danger" id="clr">Effacer la configuration MQTT</button></section>
</main><script>
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
const PORTS={mqtt:1883,mqtts:8883,ws:80,wss:443};
const ST={disabled:['Désactivé',''],connecting:['Connexion…','wait'],connected:['Connecté','on'],error:['Erreur','err']};
let loaded=false;
function rows(t,a){$(t).innerHTML=a.map(r=>'<tr><th>'+r[0]+'</th><td>'+esc(r[1])+'</td></tr>').join('');}
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+' j ':'')+h+' h '+m+' min '+s%60+' s';}
function toggle(){$('mq-fields').hidden=!$('f-en').checked;$('l-path').hidden=!$('f-scheme').value.startsWith('ws');}
$('f-en').onchange=toggle;
$('f-scheme').onchange=()=>{$('f-port').value=PORTS[$('f-scheme').value];toggle();};
async function refresh(){try{const s=await(await fetch('/api/status')).json();
$('name').textContent=s.name;
rows('wifi',[['Réseau',s.wifi.ssid],['Adresse IP',s.wifi.ip],['Signal',s.wifi.rssi+' dBm'],['Passerelle',s.wifi.gw],
['Masque',s.wifi.mask],['DNS',s.wifi.dns],['Canal',s.wifi.ch],['Adresse MAC',s.wifi.mac],
['Fonctionnement',dur(s.uptime)],['Mémoire libre',s.heap+' octets'],['OTA',s.ota]]);
const m=ST[s.mqtt.state]||[s.mqtt.state,''];$('mq-state').textContent=m[0];$('mq-dot').className='dot '+m[1];
const r=[];if(s.mqtt.uri)r.push(['Serveur',s.mqtt.uri]);
r.push(['Identifiant client',s.mqtt.clientId],['Topic de statut',s.mqtt.topic]);
if(s.mqtt.error)r.push(['Dernière erreur',s.mqtt.error]);rows('mqtt',r);
if(!loaded){loaded=true;const c=s.cfg;$('f-name').value=s.name;$('f-en').checked=c.enabled;$('f-scheme').value=c.scheme;
$('f-host').value=c.host;$('f-port').value=c.port;$('f-path').value=c.path;$('f-user').value=c.user;$('f-pass').value='';
$('f-pass').placeholder=c.passSet?'Inchangé si vide':'';toggle();}
}catch(e){}}
$('f').onsubmit=async e=>{e.preventDefault();const b=new URLSearchParams(new FormData($('f')));b.set('enabled',$('f-en').checked?'1':'0');
try{const r=await fetch('/api/mqtt',{method:'POST',body:b});const j=await r.json().catch(()=>({}));
$('msg').textContent=r.ok?'Configuration enregistrée et appliquée.':'Erreur : '+(j.error||r.status);}catch(e){$('msg').textContent='Erreur de communication';}
loaded=false;setTimeout(refresh,800);};
$('clr').onclick=async()=>{if(!confirm('Effacer la configuration MQTT ?'))return;
try{const r=await fetch('/api/mqtt/clear',{method:'POST'});$('msg').textContent=r.ok?'Configuration MQTT effacée.':'Erreur';}catch(e){}
loaded=false;setTimeout(refresh,800);};
refresh();setInterval(refresh,3000);
</script></body></html>)rawliteral";

void sendMemory(AsyncWebServerRequest *request, const char *contentType, const char *content, int code = 200) {
  request->send(code, contentType, reinterpret_cast<const uint8_t *>(content), strlen(content));
}

}  // namespace

// ---------------------------------------------------------------------------
// Cycle de vie
// ---------------------------------------------------------------------------

ESPConnect::ESPConnect() : _server(80), _ws("/ws") {}

void ESPConnect::begin(const char *deviceName, const char *apName) {
  _lock = xSemaphoreCreateMutex();
  _inbound = xQueueCreate(16, sizeof(InboundMessage *));
  _defaultName = deviceName;
  _apName = apName;

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "-%02X%02X%02X", mac[3], mac[4], mac[5]);

  Preferences p;
  p.begin(NVS_DEVICE, false);
  String name = readString(p, "name", _defaultName);
  p.end();
  if (!isValidName(name)) name = "ESPConnect";
  {
    Guard g(_lock);
    _deviceName = name;
    _clientId = name + suffix;
  }
  ESPC_LOG("Démarrage — appareil « %s », client MQTT « %s »", name.c_str(), _clientId.c_str());

  loadMqttSettings();

  if (_btnPin >= 0) {
    pinMode(_btnPin, INPUT_PULLUP);
    ESPC_LOG("Bouton de réinitialisation sur GPIO %d (appui de %lu ms)", _btnPin, (unsigned long)_btnHold);
  }

  WiFi.persistent(false);
  WiFi.onEvent(
      [this](arduino_event_id_t, arduino_event_info_t info) {
        _lastDisconnectReason = info.wifi_sta_disconnected.reason;
        ESPC_LOG("WiFi : déconnecté (motif %u : %s)", _lastDisconnectReason,
                 WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(_lastDisconnectReason)));
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  loadWifiCredentials();
  if (_ssid.isEmpty()) {
    startPortal();
  } else {
    startStation();
  }
}

void ESPConnect::loop() {
  if (!_inbound) return;  // begin() pas encore appelé
  handleResetButton();
  handleWifi();
  if (_otaStarted) ArduinoOTA.handle();

  if (_mqttApplyPending) applyPendingMqtt();
  if (_mqttClearPending) {
    _mqttClearPending = false;
    clearMqtt();
  }

  if (_mqttConnectedEvent) {
    _mqttConnectedEvent = false;
    rawPublish(statusTopic(), "online", true, 1);
    ESPC_LOG("MQTT : statut « online » publié sur « %s »", statusTopic().c_str());
    for (const auto &sub : _subscriptions) {
      int id = esp_mqtt_client_subscribe(_mqtt, sub.first.c_str(), sub.second);
      ESPC_LOG("MQTT : souscription à « %s » (QoS %u)%s", sub.first.c_str(), sub.second, id < 0 ? " — échec" : "");
    }
    _lastStatusPublish = millis();
  }
  if (isMqttConnected() && _statusInterval && millis() - _lastStatusPublish >= _statusInterval) {
    rawPublish(statusTopic(), "online", true, 1);
    _lastStatusPublish = millis();
  }

  dispatchInbound();

  static uint32_t lastCleanup = 0;
  if (millis() - lastCleanup >= 1000) {
    lastCleanup = millis();
    _ws.cleanupClients();
  }

  if (_restartAt && static_cast<int32_t>(millis() - _restartAt) >= 0) {
    ESPC_LOG("Redémarrage de l'ESP32");
    Serial.flush();
    ESP.restart();
  }
}

void ESPConnect::setResetButton(int8_t pin, uint32_t holdMs) {
  _btnPin = pin;
  _btnHold = holdMs;
}

void ESPConnect::setStatusInterval(uint32_t ms) { _statusInterval = ms; }

void ESPConnect::setMaxConnectAttempts(uint8_t attempts, uint32_t attemptTimeoutMs) {
  _maxAttempts = attempts ? attempts : 1;
  _attemptTimeout = attemptTimeoutMs;
}

void ESPConnect::setReconnectInterval(uint32_t ms) { _reconnectInterval = ms; }

void ESPConnect::enableOTA(const char *password, uint16_t port) {
  _otaEnabled = true;
  _otaPassword = password ? password : "";
  _otaPort = port;
}

String ESPConnect::deviceName() const {
  Guard g(_lock);
  return _deviceName;
}

String ESPConnect::statusTopic() const { return deviceName() + "/status"; }

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

void ESPConnect::loadWifiCredentials() {
  Preferences p;
  p.begin(NVS_WIFI, false);
  _ssid = readString(p, "ssid");
  _pass = readString(p, "pass");
  _credsValid = p.getBool("valid", false);
  p.end();
  if (_ssid.isEmpty()) {
    ESPC_LOG("WiFi : aucun identifiant enregistré");
  } else {
    ESPC_LOG("WiFi : identifiants trouvés pour « %s » (%s)", _ssid.c_str(),
             _credsValid ? "déjà validés" : "jamais validés");
  }
}

void ESPConnect::startPortal() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(_apName.c_str());
  _dns.setErrorReplyCode(DNSReplyCode::NoError);
  _dns.start(53, "*", AP_IP);
  setupPortalRoutes();
  _server.begin();
  _serverStarted = true;
  WiFi.scanNetworks(true);
  _wifiState = WifiState::Portal;
  ESPC_LOG("Portail : point d'accès ouvert « %s », page de configuration sur http://%s",
           _apName.c_str(), AP_IP.toString().c_str());
}

void ESPConnect::startStation() {
  WiFi.setHostname(deviceName().c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // la reconnexion est gérée par handleWifi()
  _attempt = 0;
  beginConnectAttempt();
}

void ESPConnect::beginConnectAttempt() {
  _attempt++;
  if (_credsValid) {
    ESPC_LOG("WiFi : connexion à « %s » (tentative %u)", _ssid.c_str(), _attempt);
  } else {
    ESPC_LOG("WiFi : connexion à « %s » (tentative %u/%u)", _ssid.c_str(), _attempt, _maxAttempts);
  }
  if (_attempt > 1) WiFi.disconnect();
  WiFi.begin(_ssid.c_str(), _pass.isEmpty() ? nullptr : _pass.c_str());
  _attemptStart = millis();
  _wifiState = WifiState::Connecting;
}

void ESPConnect::handleWifi() {
  switch (_wifiState) {
    case WifiState::Portal:
      _dns.processNextRequest();
      break;

    case WifiState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        onWifiConnected();
      } else if (millis() - _attemptStart >= _attemptTimeout) {
        if (_credsValid) {
          ESPC_LOG("WiFi : échec de la tentative, nouvel essai dans %lu s", (unsigned long)(_reconnectInterval / 1000));
          _wifiState = WifiState::Reconnecting;
          _attemptStart = millis();
        } else if (_attempt >= _maxAttempts) {
          ESPC_LOG("WiFi : échec des %u tentatives avec des identifiants jamais validés", _maxAttempts);
          eraseNamespace(NVS_WIFI);
          ESPC_LOG("WiFi : identifiants effacés, retour au portail de configuration");
          _wifiState = WifiState::Idle;
          _restartAt = millis() + 500;
        } else {
          ESPC_LOG("WiFi : échec de la tentative %u", _attempt);
          beginConnectAttempt();
        }
      }
      break;

    case WifiState::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        ESPC_LOG("WiFi : connexion perdue, nouvel essai toutes les %lu s", (unsigned long)(_reconnectInterval / 1000));
        _wifiState = WifiState::Reconnecting;
        _attempt = 0;
        _attemptStart = millis();
      }
      break;

    case WifiState::Reconnecting:
      if (WiFi.status() == WL_CONNECTED) {
        onWifiConnected();
      } else if (millis() - _attemptStart >= _reconnectInterval) {
        beginConnectAttempt();
      }
      break;

    default:
      break;
  }
}

void ESPConnect::onWifiConnected() {
  _wifiState = WifiState::Connected;
  _attempt = 0;
  ESPC_LOG("WiFi : connecté à « %s », IP %s, signal %d dBm, canal %d", _ssid.c_str(),
           WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());

  if (!_credsValid) {
    _credsValid = true;
    Preferences p;
    p.begin(NVS_WIFI, false);
    p.putBool("valid", true);
    p.end();
    ESPC_LOG("WiFi : identifiants validés et conservés");
  }

  if (!_serverStarted) {
    setupStationRoutes();
    _server.begin();
    _serverStarted = true;
    ESPC_LOG("Web : serveur démarré, statut sur http://%s/status", WiFi.localIP().toString().c_str());
  }

  if (_otaEnabled && !_otaStarted) startOTA();

  // Après une coupure WiFi, le client esp-mqtt existant se reconnecte de lui-même.
  bool mqttEnabled;
  {
    Guard g(_lock);
    mqttEnabled = _mqttCfg.enabled;
  }
  if (mqttEnabled && !_mqtt) startMqtt();
}

void ESPConnect::startOTA() {
  const String host = deviceName();
  ArduinoOTA.setHostname(host.c_str());
  ArduinoOTA.setPort(_otaPort);
  ArduinoOTA.setMdnsEnabled(false);  // téléversement par adresse IP ou nom DNS du routeur
  if (!_otaPassword.isEmpty()) ArduinoOTA.setPassword(_otaPassword.c_str());

  ArduinoOTA.onStart([this]() {
    _otaProgress = -1;
    ESPC_LOG("OTA : début de la mise à jour (%s)", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "système de fichiers");
    // Le téléversement bloque loop() : on ferme proprement MQTT et les WebSockets.
    stopMqtt(true);
    _ws.closeAll();
  });
  ArduinoOTA.onProgress([this](unsigned int done, unsigned int total) {
    int8_t step = total ? static_cast<int8_t>(done * 10ULL / total) : 0;
    if (step != _otaProgress) {
      _otaProgress = step;
      ESPC_LOG("OTA : %d %%", step * 10);
    }
  });
  ArduinoOTA.onEnd([]() { ESPC_LOG("OTA : mise à jour terminée, redémarrage"); });
  ArduinoOTA.onError([this](ota_error_t error) {
    const char *reason;
    switch (error) {
      case OTA_AUTH_ERROR: reason = "authentification refusée (mot de passe incorrect)"; break;
      case OTA_BEGIN_ERROR: reason = "démarrage impossible (espace insuffisant ?)"; break;
      case OTA_CONNECT_ERROR: reason = "connexion à l'ordinateur impossible"; break;
      case OTA_RECEIVE_ERROR: reason = "erreur de réception"; break;
      case OTA_END_ERROR: reason = "erreur de finalisation"; break;
      default: reason = "erreur inconnue"; break;
    }
    ESPC_LOG("OTA : échec — %s", reason);
    // La mise à jour a échoué sans redémarrage : on relance MQTT s'il avait été arrêté.
    bool mqttEnabled;
    {
      Guard g(_lock);
      mqttEnabled = _mqttCfg.enabled;
    }
    if (mqttEnabled && !_mqtt && isWifiConnected()) startMqtt();
  });

  ArduinoOTA.begin();
  _otaStarted = true;
  ESPC_LOG("OTA : prêt sur %s:%u (%s)", WiFi.localIP().toString().c_str(), _otaPort,
           _otaPassword.isEmpty() ? "SANS mot de passe" : "protégé par mot de passe");
}

void ESPConnect::handleResetButton() {
  if (_btnPin < 0) return;
  if (digitalRead(_btnPin) != LOW) {
    _btnPressed = false;
    _btnFired = false;
    return;
  }
  if (!_btnPressed) {
    _btnPressed = true;
    _btnStart = millis();
  } else if (!_btnFired && millis() - _btnStart >= _btnHold) {
    _btnFired = true;
    ESPC_LOG("Bouton : appui maintenu %lu ms détecté", (unsigned long)_btnHold);
    resetWifi();
  }
}

void ESPConnect::resetWifi() {
  eraseNamespace(NVS_WIFI);
  ESPC_LOG("WiFi : identifiants effacés, redémarrage en mode point d'accès");
  _restartAt = millis() + 200;
}

// ---------------------------------------------------------------------------
// Routes web
// ---------------------------------------------------------------------------

void ESPConnect::setupPortalRoutes() {
  _server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) { sendMemory(r, "text/html", PORTAL_HTML); });

  _server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *r) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      r->send(202, "application/json", "[]");
      return;
    }
    if (n == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(true);
      r->send(202, "application/json", "[]");
      return;
    }
    std::map<String, std::pair<int32_t, bool>> nets;  // SSID -> (meilleur RSSI, sécurisé)
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.isEmpty()) continue;
      auto it = nets.find(ssid);
      if (it == nets.end() || WiFi.RSSI(i) > it->second.first) {
        nets[ssid] = {WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
      }
    }
    WiFi.scanDelete();
    String json = "[";
    for (const auto &net : nets) {
      if (json.length() > 1) json += ',';
      json += "{\"ssid\":\"" + jsonEscape(net.first) + "\",\"rssi\":" + String(net.second.first) +
              ",\"secure\":" + (net.second.second ? "true" : "false") + "}";
    }
    json += "]";
    r->send(200, "application/json", json);
  });

  _server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *r) {
    String ssid = r->hasParam("ssid", true) ? r->getParam("ssid", true)->value() : String();
    String pass = r->hasParam("pass", true) ? r->getParam("pass", true)->value() : String();
    if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 63 || (pass.length() > 0 && pass.length() < 8)) {
      ESPC_LOG("Portail : identifiants refusés (SSID 1 à 32 caractères, mot de passe vide ou 8 à 63)");
      r->send(400, "text/html; charset=utf-8",
              "<meta charset=utf-8><p>SSID (1 à 32 caractères) ou mot de passe (vide ou 8 à 63 caractères) "
              "invalide.</p><p><a href=\"/\">Retour</a></p>");
      return;
    }
    Preferences p;
    p.begin(NVS_WIFI, false);
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.putBool("valid", false);
    p.end();
    ESPC_LOG("Portail : identifiants enregistrés pour « %s »", ssid.c_str());
    sendMemory(r, "text/html", SAVED_HTML);
    _restartAt = millis() + 1500;
  });

  // Portail captif : toute autre requête (tests de connectivité Android, iOS, Windows…)
  // est redirigée vers la page de configuration.
  _server.onNotFound([](AsyncWebServerRequest *r) { r->redirect("http://" + AP_IP.toString() + "/"); });
}

void ESPConnect::setupStationRoutes() {
  // Routes utilisateur en premier : elles sont prioritaires.
  for (const auto &route : _routes) registerRoute(route);

  _server.on("/status", HTTP_GET, [](AsyncWebServerRequest *r) { sendMemory(r, "text/html", STATUS_HTML); });
  _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *r) { handleStatusJson(r); });
  // "/api/mqtt/clear" avant "/api/mqtt", qui sinon le capturerait comme sous-chemin.
  _server.on("/api/mqtt/clear", HTTP_POST, [this](AsyncWebServerRequest *r) {
    ESPC_LOG("Web : demande d'effacement de la configuration MQTT");
    _mqttClearPending = true;
    r->send(200, "application/json", "{\"ok\":true}");
  });
  _server.on("/api/mqtt", HTTP_POST, [this](AsyncWebServerRequest *r) { handleMqttPost(r); });

  _ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg,
                     uint8_t *data, size_t len) { onWsEvent(server, client, type, arg, data, len); });
  _server.addHandler(&_ws);

  if (!_rootRouteDefined) {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) { r->redirect("/status"); });
  }
  _server.onNotFound([](AsyncWebServerRequest *r) { r->send(404, "text/plain; charset=utf-8", "Page introuvable"); });
}

void ESPConnect::addRoute(const char *path, WebRequestMethodComposite method, const char *content,
                          const char *contentType) {
  Route route{path, method, content, contentType};
  _routes.push_back(route);
  if (_serverStarted && _wifiState != WifiState::Portal) registerRoute(route);
  ESPC_LOG("Web : route « %s » déclarée", path);
}

void ESPConnect::registerRoute(const Route &route) {
  const char *content = route.content;
  String type = route.contentType;
  _server.on(route.path.c_str(), route.method,
             [content, type](AsyncWebServerRequest *r) { sendMemory(r, type.c_str(), content); });
  if (route.path == "/") _rootRouteDefined = true;
}

void ESPConnect::handleStatusJson(AsyncWebServerRequest *request) {
  MqttSettings cfg;
  String name, clientId, error;
  {
    Guard g(_lock);
    cfg = _mqttCfg;
    name = _deviceName;
    clientId = _clientId;
    error = _mqttError;
  }
  String j;
  j.reserve(900);
  j += "{\"name\":\"" + jsonEscape(name) + "\"";
  j += ",\"uptime\":" + String(millis() / 1000);
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  String ota = "désactivé";
  if (_otaStarted) ota = "port " + String(_otaPort) + (_otaPassword.isEmpty() ? ", sans mot de passe" : ", avec mot de passe");
  j += ",\"ota\":\"" + ota + "\"";
  j += ",\"wifi\":{\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
  j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  j += ",\"gw\":\"" + WiFi.gatewayIP().toString() + "\"";
  j += ",\"mask\":\"" + WiFi.subnetMask().toString() + "\"";
  j += ",\"dns\":\"" + WiFi.dnsIP().toString() + "\"";
  j += ",\"mac\":\"" + WiFi.macAddress() + "\"";
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"ch\":" + String(WiFi.channel()) + "}";
  j += ",\"mqtt\":{\"state\":\"" + String(mqttStateName(_mqttState)) + "\"";
  j += ",\"uri\":\"" + jsonEscape(cfg.enabled ? mqttUri(cfg) : String()) + "\"";
  j += ",\"clientId\":\"" + jsonEscape(clientId) + "\"";
  j += ",\"topic\":\"" + jsonEscape(name + "/status") + "\"";
  j += ",\"error\":\"" + jsonEscape(error) + "\"}";
  j += ",\"cfg\":{\"enabled\":" + String(cfg.enabled ? "true" : "false");
  j += ",\"scheme\":\"" + cfg.scheme + "\"";
  j += ",\"host\":\"" + jsonEscape(cfg.host) + "\"";
  j += ",\"port\":" + String(cfg.port);
  j += ",\"path\":\"" + jsonEscape(cfg.path) + "\"";
  j += ",\"user\":\"" + jsonEscape(cfg.user) + "\"";
  j += ",\"passSet\":" + String(cfg.pass.isEmpty() ? "false" : "true") + "}}";
  request->send(200, "application/json", j);
}

void ESPConnect::handleMqttPost(AsyncWebServerRequest *request) {
  auto arg = [request](const char *n) -> String {
    if (!request->hasParam(n, true)) return String();
    String v = request->getParam(n, true)->value();
    v.trim();
    return v;
  };
  auto fail = [request](const char *msg) {
    ESPC_LOG("Web : configuration refusée — %s", msg);
    request->send(400, "application/json", "{\"error\":\"" + jsonEscape(msg) + "\"}");
  };

  String name = arg("name");
  if (!isValidName(name)) return fail("Nom d'appareil invalide (1 à 32 caractères : lettres, chiffres, - et _)");

  MqttSettings s;
  s.enabled = arg("enabled") == "1";
  s.scheme = arg("scheme");
  s.host = arg("host");
  long port = arg("port").toInt();
  s.path = arg("path");
  s.user = arg("user");
  s.pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : String();

  if (!isValidScheme(s.scheme)) {
    if (s.enabled) return fail("Protocole invalide");
    s.scheme = "wss";
  }
  if (port < 1 || port > 65535) {
    if (s.enabled) return fail("Port invalide (1 à 65535)");
    port = defaultPort(s.scheme);
  }
  s.port = static_cast<uint16_t>(port);
  if (s.enabled && s.host.isEmpty()) return fail("Adresse du serveur manquante");
  if (s.path.isEmpty()) s.path = "/mqtt";
  if (!s.path.startsWith("/")) s.path = "/" + s.path;

  {
    Guard g(_lock);
    if (s.pass.isEmpty()) s.pass = _mqttCfg.pass;  // champ vide : mot de passe conservé
    _pendingMqtt = s;
    _pendingName = name;
  }
  _mqttApplyPending = true;
  ESPC_LOG("Web : nouvelle configuration reçue");
  request->send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

void ESPConnect::onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg,
                           uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      ESPC_LOG("WebSocket : client #%lu connecté (%s)", (unsigned long)client->id(),
               client->remoteIP().toString().c_str());
      break;

    case WS_EVT_DISCONNECT:
      ESPC_LOG("WebSocket : client #%lu déconnecté", (unsigned long)client->id());
      _wsBuffers.erase(client->id());
      break;

    case WS_EVT_DATA: {
      auto *info = static_cast<AwsFrameInfo *>(arg);
      String &buf = _wsBuffers[client->id()];
      if (info->index == 0 && info->num == 0) buf = "";  // début d'un nouveau message
      buf.concat(data, len);
      if (info->final && info->index + len == info->len) {
        if (info->message_opcode == WS_TEXT) {
          pushInbound(new InboundMessage{String(), buf, client->id()});
        } else {
          ESPC_LOG("WebSocket : message binaire du client #%lu ignoré", (unsigned long)client->id());
        }
        _wsBuffers.erase(client->id());
      }
      break;
    }

    default:
      break;
  }
}

bool ESPConnect::wsSend(const String &message) {
  if (_ws.count() == 0) return false;
  return _ws.textAll(message) != AsyncWebSocket::DISCARDED;
}

bool ESPConnect::wsSend(uint32_t clientId, const String &message) { return _ws.text(clientId, message); }

void ESPConnect::pushInbound(InboundMessage *msg) {
  if (xQueueSend(_inbound, &msg, 0) != pdTRUE) {
    ESPC_LOG("File des messages pleine, message perdu");
    delete msg;
  }
}

void ESPConnect::dispatchInbound() {
  InboundMessage *msg;
  while (xQueueReceive(_inbound, &msg, 0) == pdTRUE) {
    if (msg->clientId == 0) {
      if (_mqttCallback) _mqttCallback(msg->topic, msg->payload);
    } else if (_wsCallback) {
      _wsCallback(msg->clientId, msg->payload);
    }
    delete msg;
  }
}

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------

void ESPConnect::loadMqttSettings() {
  MqttSettings s;
  Preferences p;
  p.begin(NVS_MQTT, false);
  s.enabled = p.getBool("en", false);
  s.scheme = readString(p, "scheme", s.scheme);
  s.host = readString(p, "host");
  s.port = p.getUShort("port", s.port);
  s.path = readString(p, "path", s.path);
  s.user = readString(p, "user");
  s.pass = readString(p, "pass");
  p.end();
  {
    Guard g(_lock);
    _mqttCfg = s;
  }
  if (s.host.isEmpty()) {
    ESPC_LOG("MQTT : aucune configuration enregistrée");
  } else {
    ESPC_LOG("MQTT : configuration chargée (%s, %s)", mqttUri(s).c_str(), s.enabled ? "activée" : "désactivée");
  }
}

void ESPConnect::saveMqttSettings(const MqttSettings &s) {
  Preferences p;
  p.begin(NVS_MQTT, false);
  p.putBool("en", s.enabled);
  p.putString("scheme", s.scheme);
  p.putString("host", s.host);
  p.putUShort("port", s.port);
  p.putString("path", s.path);
  p.putString("user", s.user);
  p.putString("pass", s.pass);
  p.end();
  {
    Guard g(_lock);
    _mqttCfg = s;
  }
  ESPC_LOG("MQTT : configuration enregistrée (%s, %s)", mqttUri(s).c_str(), s.enabled ? "activée" : "désactivée");
}

String ESPConnect::mqttUri(const MqttSettings &s) {
  String uri = s.scheme + "://" + s.host + ":" + String(s.port);
  if (s.scheme.startsWith("ws")) uri += s.path;
  return uri;
}

void ESPConnect::applyPendingMqtt() {
  MqttSettings s;
  String name;
  {
    Guard g(_lock);
    s = _pendingMqtt;
    name = _pendingName;
    _mqttApplyPending = false;
  }

  // Arrêt avec "offline" sur l'ancien topic avant tout changement.
  stopMqtt(true);

  if (name != deviceName()) {
    Preferences p;
    p.begin(NVS_DEVICE, false);
    p.putString("name", name);
    p.end();
    {
      Guard g(_lock);
      _clientId = name + _clientId.substring(_deviceName.length());  // conserve le suffixe MAC
      _deviceName = name;
    }
    ESPC_LOG("Appareil renommé « %s » (nom d'hôte appliqué au prochain démarrage)", name.c_str());
  }

  saveMqttSettings(s);
  if (s.enabled && isWifiConnected()) startMqtt();
}

void ESPConnect::clearMqtt() {
  stopMqtt(true);
  eraseNamespace(NVS_MQTT);
  {
    Guard g(_lock);
    _mqttCfg = MqttSettings();
    _mqttError = "";
  }
  ESPC_LOG("MQTT : configuration effacée");
}

void ESPConnect::startMqtt() {
  if (_mqtt) stopMqtt(false);

  MqttSettings s;
  String clientId;
  {
    Guard g(_lock);
    s = _mqttCfg;
    clientId = _clientId;
    _mqttError = "";
  }
  if (!s.enabled || s.host.isEmpty()) return;

  const String uri = mqttUri(s);
  const String topic = statusTopic();

  esp_mqtt_client_config_t cfg = {};
  cfg.uri = uri.c_str();
  cfg.client_id = clientId.c_str();
  if (!s.user.isEmpty()) cfg.username = s.user.c_str();
  if (!s.pass.isEmpty()) cfg.password = s.pass.c_str();
  cfg.lwt_topic = topic.c_str();
  cfg.lwt_msg = "offline";
  cfg.lwt_qos = 1;
  cfg.lwt_retain = 1;
  cfg.keepalive = 30;
  cfg.reconnect_timeout_ms = 5000;
  cfg.network_timeout_ms = 10000;
  cfg.buffer_size = 2048;
  cfg.task_stack = 8192;
  if (s.scheme == "mqtts" || s.scheme == "wss") {
    cfg.crt_bundle_attach = insecureTlsAttach;
    cfg.skip_cert_common_name_check = true;
  }

  // esp_mqtt_client_init copie toutes les chaînes de la configuration.
  _mqtt = esp_mqtt_client_init(&cfg);
  if (!_mqtt) {
    Guard g(_lock);
    _mqttError = "initialisation du client impossible";
    _mqttState = MqttState::Error;
    ESPC_LOG("MQTT : initialisation du client impossible");
    return;
  }
  esp_mqtt_client_register_event(_mqtt, MQTT_EVENT_ANY, &ESPConnect::mqttEventHandler, this);
  _mqttState = MqttState::Connecting;
  ESPC_LOG("MQTT : connexion à %s (client « %s », testament sur « %s »)", uri.c_str(), clientId.c_str(), topic.c_str());
  if (esp_mqtt_client_start(_mqtt) != ESP_OK) {
    ESPC_LOG("MQTT : démarrage du client impossible");
    esp_mqtt_client_destroy(_mqtt);
    _mqtt = nullptr;
    _mqttState = MqttState::Error;
  }
}

void ESPConnect::stopMqtt(bool announceOffline) {
  if (!_mqtt) return;
  if (announceOffline && _mqttState == MqttState::Connected) {
    // Déconnexion volontaire : le broker n'enverra pas le testament, on publie "offline" nous-mêmes.
    esp_mqtt_client_publish(_mqtt, statusTopic().c_str(), "offline", 0, 1, 1);
    ESPC_LOG("MQTT : statut « offline » publié sur « %s »", statusTopic().c_str());
    delay(200);
  }
  esp_mqtt_client_destroy(_mqtt);  // arrête la tâche et ferme la connexion
  _mqtt = nullptr;
  _mqttState = MqttState::Disabled;
  _mqttConnectedEvent = false;
  ESPC_LOG("MQTT : connexion au broker fermée");
}

void ESPConnect::mqttEventHandler(void *arg, esp_event_base_t, int32_t, void *data) {
  static_cast<ESPConnect *>(arg)->onMqttEvent(static_cast<esp_mqtt_event_handle_t>(data));
}

// Appelé dans la tâche esp-mqtt : rien de bloquant, le traitement se fait dans loop().
void ESPConnect::onMqttEvent(esp_mqtt_event_handle_t e) {
  switch (e->event_id) {
    case MQTT_EVENT_BEFORE_CONNECT:
      if (_mqttState != MqttState::Error) _mqttState = MqttState::Connecting;
      break;

    case MQTT_EVENT_CONNECTED: {
      Guard g(_lock);
      _mqttError = "";
      _mqttState = MqttState::Connected;
      _mqttConnectedEvent = true;
      ESPC_LOG("MQTT : connecté au broker");
      break;
    }

    case MQTT_EVENT_DISCONNECTED:
      if (_mqttState == MqttState::Connected) {
        _mqttState = MqttState::Connecting;
        ESPC_LOG("MQTT : déconnecté du broker, reconnexion automatique");
      }
      break;

    case MQTT_EVENT_SUBSCRIBED:
      ESPC_LOG("MQTT : souscription confirmée (message %d)", e->msg_id);
      break;

    case MQTT_EVENT_UNSUBSCRIBED:
      ESPC_LOG("MQTT : désinscription confirmée (message %d)", e->msg_id);
      break;

    case MQTT_EVENT_DATA:
      // Les messages plus grands que le tampon arrivent en plusieurs fragments.
      if (e->current_data_offset == 0) {
        _rxTopic = String(e->topic, e->topic_len);
        _rxPayload = "";
        _rxPayload.reserve(e->total_data_len);
      }
      _rxPayload.concat(e->data, e->data_len);
      if (e->current_data_offset + e->data_len >= e->total_data_len) {
        ESPC_LOG("MQTT : reçu « %s » = %s", _rxTopic.c_str(), shorten(_rxPayload).c_str());
        pushInbound(new InboundMessage{_rxTopic, _rxPayload, 0});
      }
      break;

    case MQTT_EVENT_ERROR: {
      String reason;
      const esp_mqtt_error_codes_t *eh = e->error_handle;
      if (eh->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        reason = "transport";
        if (eh->esp_tls_last_esp_err) reason += String(" : ") + esp_err_to_name(eh->esp_tls_last_esp_err);
        if (eh->esp_tls_stack_err) {
          char buf[24];
          snprintf(buf, sizeof(buf), ", TLS -0x%04X", static_cast<unsigned>(-eh->esp_tls_stack_err));
          reason += buf;
        }
        if (eh->esp_transport_sock_errno) reason += String(", socket : ") + strerror(eh->esp_transport_sock_errno);
      } else if (eh->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        reason = "connexion refusée : ";
        switch (eh->connect_return_code) {
          case MQTT_CONNECTION_REFUSE_PROTOCOL: reason += "version de protocole refusée"; break;
          case MQTT_CONNECTION_REFUSE_ID_REJECTED: reason += "identifiant client refusé"; break;
          case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE: reason += "serveur indisponible"; break;
          case MQTT_CONNECTION_REFUSE_BAD_USERNAME: reason += "utilisateur ou mot de passe incorrect"; break;
          case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED: reason += "non autorisé"; break;
          default: reason += "code " + String(static_cast<int>(eh->connect_return_code)); break;
        }
      } else {
        reason = "erreur inconnue";
      }
      {
        Guard g(_lock);
        _mqttError = reason;
      }
      _mqttState = MqttState::Error;
      ESPC_LOG("MQTT : échec (%s), nouvel essai dans 5 s", reason.c_str());
      break;
    }

    default:
      break;
  }
}

bool ESPConnect::subscribe(const String &topic, uint8_t qos) {
  if (topic.isEmpty()) return false;
  if (qos > 2) qos = 2;
  _subscriptions[topic] = qos;
  if (!isMqttConnected()) {
    ESPC_LOG("MQTT : souscription à « %s » mémorisée (appliquée à la connexion)", topic.c_str());
    return true;
  }
  int id = esp_mqtt_client_subscribe(_mqtt, topic.c_str(), qos);
  ESPC_LOG("MQTT : souscription à « %s » (QoS %u)%s", topic.c_str(), qos, id < 0 ? " — échec" : "");
  return id >= 0;
}

bool ESPConnect::unsubscribe(const String &topic) {
  _subscriptions.erase(topic);
  if (!isMqttConnected()) {
    ESPC_LOG("MQTT : souscription à « %s » retirée", topic.c_str());
    return true;
  }
  int id = esp_mqtt_client_unsubscribe(_mqtt, topic.c_str());
  ESPC_LOG("MQTT : désinscription de « %s »%s", topic.c_str(), id < 0 ? " — échec" : "");
  return id >= 0;
}

bool ESPConnect::rawPublish(const String &topic, const String &payload, bool retain, uint8_t qos) {
  if (!_mqtt || !isMqttConnected()) return false;
  return esp_mqtt_client_publish(_mqtt, topic.c_str(), payload.c_str(), payload.length(), qos > 2 ? 2 : qos,
                                 retain) >= 0;
}

bool ESPConnect::publish(const String &topic, const String &payload, bool retain, uint8_t qos) {
  if (!isMqttConnected()) {
    ESPC_LOG("MQTT : publication sur « %s » ignorée (non connecté)", topic.c_str());
    return false;
  }
  bool ok = rawPublish(topic, payload, retain, qos);
  ESPC_LOG("MQTT : publication « %s » = %s%s%s", topic.c_str(), shorten(payload).c_str(), retain ? " (retenu)" : "",
           ok ? "" : " — échec");
  return ok;
}

bool ESPConnect::publish(const String &topic, const char *payload, bool retain, uint8_t qos) {
  return publish(topic, String(payload), retain, qos);
}

bool ESPConnect::publish(const String &topic, bool value, bool retain, uint8_t qos) {
  return publish(topic, String(value ? "true" : "false"), retain, qos);
}

bool ESPConnect::publish(const String &topic, int value, bool retain, uint8_t qos) {
  return publish(topic, String(value), retain, qos);
}

bool ESPConnect::publish(const String &topic, unsigned int value, bool retain, uint8_t qos) {
  return publish(topic, String(value), retain, qos);
}

bool ESPConnect::publish(const String &topic, long value, bool retain, uint8_t qos) {
  return publish(topic, String(value), retain, qos);
}

bool ESPConnect::publish(const String &topic, unsigned long value, bool retain, uint8_t qos) {
  return publish(topic, String(value), retain, qos);
}

bool ESPConnect::publish(const String &topic, float value, bool retain, uint8_t qos, uint8_t decimals) {
  return publish(topic, String(value, static_cast<unsigned int>(decimals)), retain, qos);
}

bool ESPConnect::publish(const String &topic, double value, bool retain, uint8_t qos, uint8_t decimals) {
  return publish(topic, String(value, static_cast<unsigned int>(decimals)), retain, qos);
}

// ---------------------------------------------------------------------------
// Utilitaires
// ---------------------------------------------------------------------------

String ESPConnect::jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}
