# ESPConnect

Librairie ESP32 (framework Arduino, PlatformIO) qui prend en charge la connexion d'un appareil :

- portail captif de configuration WiFi au premier démarrage ;
- stockage des identifiants en mémoire flash (NVS) et reconnexion automatique ;
- bouton de réinitialisation du WiFi ;
- page de statut `/status` avec configuration MQTT à chaud ;
- client MQTT (TCP, TLS, WebSocket, WebSocket sécurisé) avec statut `online`/`offline` et testament (LWT) ;
- routes web personnalisées et échanges WebSocket avec les pages ;
- mise à jour du firmware par le réseau (OTA), protégée par mot de passe.

## Dépendances

À déclarer dans `platformio.ini` :

```ini
lib_deps =
	ESP32Async/AsyncTCP @ 3.5.0
	ESP32Async/ESPAsyncWebServer @ 3.12.0
```

Le client MQTT est `esp-mqtt`, fourni avec le framework arduino-esp32 (testé avec la version 2.0.17) : aucune dépendance supplémentaire.

## Exemple

```cpp
#include <Arduino.h>
#include <ESPConnect.h>

// Page de démonstration servie sur /semis : échange de messages via le WebSocket /ws.
const char SEMIS_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>SemiX</title></head><body>
<h1>SemiX</h1><p id="state">Connexion…</p>
<button onclick="ws.send('ping')">Envoyer ping</button><pre id="log"></pre>
<script>
const ws = new WebSocket('ws://' + location.host + '/ws');
ws.onopen = () => document.getElementById('state').textContent = 'Connecté';
ws.onclose = () => document.getElementById('state').textContent = 'Déconnecté';
ws.onmessage = e => document.getElementById('log').textContent += e.data + '\n';
</script></body></html>)rawliteral";

void setup() {
  Serial.begin(115200);

  espConnect.addRoute("/semis", HTTP_GET, SEMIS_HTML);

  espConnect.onWsMessage([](uint32_t clientId, const String &message) {
    if (message == "ping") espConnect.wsSend(clientId, "pong");
  });

  espConnect.onMqttMessage([](const String &topic, const String &payload) {
    Serial.printf("Message MQTT %s : %s\n", topic.c_str(), payload.c_str());
  });
  espConnect.subscribe("semix/cmd/#");

  // Enable OTA updates with the password "gregory"
  espConnect.enableOTA("gregory");

  espConnect.begin("semix", "SemiX-Setup");
}

void loop() {
  espConnect.loop();

  static uint32_t last = 0;
  if (millis() - last >= 30000) {
    last = millis();
    espConnect.wsSend("uptime " + String(millis() / 1000) + " s");
  }
}

```

## Fonctionnement

### Premier démarrage : portail de configuration

Sans identifiants en mémoire, l'ESP32 ouvre un point d'accès **sans mot de passe** (nom passé à `begin()`, `ESPConnect` par défaut). En s'y connectant, la page de configuration s'ouvre automatiquement sur la plupart des téléphones ; sinon elle est accessible à **http://192.168.1.1**.

La page liste les réseaux détectés (un clic remplit le SSID) et propose un champ SSID libre (réseaux masqués) et un champ mot de passe (vide pour un réseau ouvert, sinon 8 à 63 caractères). Après validation, les identifiants sont écrits en NVS et l'ESP32 redémarre.

L'adresse du point d'accès peut être changée à la compilation : `-DESPCONNECT_AP_IP=10,0,0,1`.

### Connexion

- **Identifiants jamais validés** : 5 tentatives de 15 s (réglable avec `setMaxConnectAttempts()`). Si toutes échouent, les identifiants sont effacés et l'ESP32 redémarre sur le portail.
- **Première connexion réussie** : les identifiants sont marqués valides et ne seront plus jamais effacés automatiquement.
- **Démarrages suivants** : les identifiants enregistrés sont utilisés directement.
- **Perte du signal** : nouvelle tentative toutes les 10 s (`setReconnectInterval()`) jusqu'au retour du réseau. Le client MQTT se reconnecte ensuite de lui-même.

### Bouton de réinitialisation

Un appui maintenu **2 s** sur le bouton **BOOT (GPIO 0)** efface les identifiants WiFi et redémarre l'ESP32 en mode point d'accès. La configuration MQTT et le nom d'appareil sont conservés.

```cpp
espConnect.setResetButton(15, 5000);  // GPIO 15, 5 s (à appeler avant begin())
espConnect.setResetButton(-1);        // désactive le bouton
```

Le bouton est câblé entre la broche et GND (résistance de tirage interne activée).

### Mise à jour par le réseau (OTA)

```cpp
espConnect.enableOTA("mon-mot-de-passe");        // port 3232 par défaut
espConnect.enableOTA("mon-mot-de-passe", 8266);  // autre port
```

À appeler avant `begin()`. Le service OTA (ArduinoOTA, protocole `espota` de PlatformIO) démarre dès la première connexion WiFi ; il n'est pas disponible sur le portail de configuration. Un mot de passe vide ou `nullptr` désactive l'authentification (déconseillé).

Côté PlatformIO, un environnement dédié dans `platformio.ini` :

```ini
[env:az-delivery-devkit-v4_ota]
extends = env:az-delivery-devkit-v4
upload_protocol = espota
upload_port = 192.168.10.52       ; adresse IP de l'ESP32, ou son nom DNS sur le réseau local
upload_flags =
	--auth=mon-mot-de-passe        ; le même que dans enableOTA()
	--port=3232
```

puis `pio run -e az-delivery-devkit-v4_ota -t upload`.

- Le mDNS n'est pas activé : `upload_port` doit être l'adresse IP de l'ESP32 (visible sur `/status`) ou le nom que lui donne le DNS de votre routeur, construit à partir du nom d'appareil.
- Au début du téléversement, l'ESP32 publie `offline`, ferme la connexion MQTT et les WebSockets, puis redémarre sur le nouveau firmware. En cas d'échec, la connexion MQTT est relancée.
- La progression (par paliers de 10 %) et les erreurs (mot de passe refusé, espace insuffisant…) apparaissent dans le journal série.
- La page `/status` indique si l'OTA est actif, son port et la présence d'un mot de passe.
- La table de partitions doit comporter deux emplacements d'application (cas de la table par défaut de l'ESP32).

### Page de statut

Une fois connecté, l'ESP32 sert **http://&lt;ip-esp32&gt;/status** (la racine `/` y redirige, sauf si une route `/` est déclarée). La page se rafraîchit toutes les 3 s et affiche :

- WiFi : réseau, IP, signal, passerelle, masque, DNS, canal, MAC, temps de fonctionnement, mémoire libre, état de l'OTA ;
- MQTT : état (désactivé, connexion…, connecté, erreur), serveur, identifiant client, topic de statut et dernière erreur.

Le formulaire permet de modifier :

- le **nom d'appareil** (lettres, chiffres, `-`, `_`, 32 caractères au plus) ; il sert d'identifiant client et de préfixe du topic de statut. Le nom d'hôte réseau n'est mis à jour qu'au prochain démarrage ;
- la case **Activer la connexion MQTT**, qui affiche : protocole, serveur, port, chemin WebSocket, utilisateur, mot de passe.

| Protocole | Port par défaut | Transport |
|---|---|---|
| `mqtt://` | 1883 | TCP |
| `mqtts://` | 8883 | TLS |
| `ws://` | 80 | WebSocket (chemin, `/mqtt` par défaut) |
| `wss://` | 443 | WebSocket sur TLS |

En TLS (`mqtts`, `wss`), **le certificat du serveur n'est pas vérifié** : la liaison est chiffrée mais n'est pas protégée contre une usurpation du serveur.

Les paramètres sont écrits en NVS (espace `espc_mqtt`, séparé du WiFi `espc_wifi`) et appliqués immédiatement, sans redémarrage. Laisser le mot de passe vide conserve le mot de passe déjà enregistré ; pour le supprimer, effacer la configuration MQTT puis la ressaisir.

Le bouton **Effacer la configuration MQTT** publie `offline`, ferme la connexion au broker et efface les paramètres, sans redémarrage.

La page n'est pas protégée par mot de passe : tout appareil du réseau local peut y accéder.

### Statut et testament (LWT)

Le topic de statut est `<nom d'appareil>/status` (par exemple `semix/status`).

- À la connexion puis toutes les 60 s (`setStatusInterval()`), l'ESP32 y publie `online` (retenu, QoS 1).
- Ce topic est déclaré comme testament : en cas de déconnexion brutale (coupure de courant, perte du signal), le broker y publie lui-même `offline` (retenu) après expiration du keepalive (30 s × 1,5).
- Lors d'une déconnexion volontaire (changement ou effacement de la configuration), l'ESP32 publie `offline` avant de fermer la connexion.

L'identifiant client MQTT est `<nom d'appareil>-<3 derniers octets de la MAC>`, par exemple `semix-A1B2C3`.

## API

### Cycle de vie et réglages

| Méthode | Rôle |
|---|---|
| `begin(deviceName = "ESPConnect", apName = "ESPConnect")` | Démarre la librairie. Le nom enregistré depuis `/status` est prioritaire sur `deviceName`. |
| `loop()` | À appeler dans `loop()` : WiFi, bouton, MQTT, callbacks. |
| `setResetButton(pin = 0, holdMs = 2000)` | Bouton de réinitialisation (`-1` pour désactiver). |
| `setStatusInterval(ms = 60000)` | Période de publication de `online` (`0` : seulement à la connexion). |
| `setMaxConnectAttempts(n = 5, timeoutMs = 15000)` | Tentatives pour des identifiants jamais validés. |
| `setReconnectInterval(ms = 10000)` | Délai entre deux tentatives après une perte du WiFi. |
| `enableOTA(password, port = 3232)` | Active la mise à jour par le réseau. |
| `resetWifi()` | Efface les identifiants WiFi et redémarre sur le portail. |
| `clearMqtt()` | Ferme la connexion MQTT et efface sa configuration. |

### État

`isWifiConnected()`, `isMqttConnected()`, `isPortalActive()`, `isOTAEnabled()`, `wifiState()`, `mqttState()`, `deviceName()`, `statusTopic()`.

### MQTT

```cpp
bool subscribe(const String &topic, uint8_t qos = 0);   // jokers + et # acceptés
bool unsubscribe(const String &topic);
void onMqttMessage(std::function<void(const String &topic, const String &payload)> cb);

bool publish(const String &topic, const String &payload, bool retain = false, uint8_t qos = 0);
// Surcharges : const char*, bool ("true"/"false"), int, unsigned int, long, unsigned long,
// float et double (paramètre supplémentaire decimals = 2 en dernière position).
espConnect.publish("semix/temp", 21.437f, false, 0, 1);  // publie "21.4"
```

Les souscriptions peuvent être déclarées à tout moment, même avant la connexion : elles sont mémorisées et renouvelées à chaque (re)connexion. `publish()` renvoie `false` si le broker n'est pas connecté.

### Routes web

```cpp
void addRoute(const char *path, WebRequestMethodComposite method, const char *content,
              const char *contentType = "text/html");

espConnect.addRoute("/semis", HTTP_GET, SEMIS_HTML);
espConnect.addRoute("/api/info", HTTP_GET, INFO_JSON, "application/json");
```

`content` est une chaîne en mémoire (`const char[] PROGMEM`) qui doit rester valide pendant toute l'exécution ; elle est envoyée sans copie. Déclarer les routes avant `begin()` : elles sont alors prioritaires sur les routes internes (`/`, `/status`…). Pour des réponses dynamiques, `server()` donne accès à l'`AsyncWebServer`.

Les routes ne sont servies qu'en mode connecté, pas sur le portail.

### WebSocket

Le point d'accès WebSocket est `ws://<ip-esp32>/ws`.

```cpp
void onWsMessage(std::function<void(uint32_t clientId, const String &message)> cb);
bool wsSend(const String &message);                     // à tous les clients
bool wsSend(uint32_t clientId, const String &message);  // à un client
size_t wsClientCount();
```

Les messages sont du texte brut (à vous d'y mettre du JSON si besoin). Les messages binaires sont ignorés.

Les callbacks MQTT et WebSocket sont appelés depuis `espConnect.loop()`, dans le contexte de la boucle principale : ils peuvent appeler `publish()`, `wsSend()` ou piloter les sorties sans précaution particulière. Jusqu'à 16 messages sont mis en attente entre deux appels de `loop()`.

## Journal série

Chaque étape est écrite sur le port série, préfixée par `[ESPConnect]` : tentatives et échecs de connexion WiFi, déconnexions avec leur motif, adresse IP obtenue, ouverture du portail, enregistrement ou effacement de la configuration, connexions et erreurs MQTT (avec la cause : DNS, TLS, identifiants refusés…), publications, souscriptions, messages reçus, clients WebSocket.

Le journal est écrit en ASCII pur (accents retirés, `« »` remplacés par `"`) afin de s'afficher correctement dans tous les moniteurs série.

Pour désactiver le journal : `build_flags = -DESPCONNECT_NO_LOG`.

## Mémoire NVS

| Espace | Clés | Effacé par |
|---|---|---|
| `espc_wifi` | `ssid`, `pass`, `valid` | bouton, `resetWifi()`, échec des tentatives initiales |
| `espc_mqtt` | `en`, `scheme`, `host`, `port`, `path`, `user`, `pass` | bouton « Effacer » de `/status`, `clearMqtt()` |
| `espc_dev` | `name` | — |
