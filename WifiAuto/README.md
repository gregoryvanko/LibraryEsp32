# WifiAuto

Librairie de connexion WiFi + MQTT automatique pour ESP32 (Arduino / PlatformIO).

Au premier démarrage, l'ESP32 crée son propre point d'accès WiFi pour permettre
de lui indiquer, via une page web, le réseau auquel se connecter. Une fois
connecté à ce réseau, une seconde page permet de configurer un broker MQTT.
Les deux configurations sont enregistrées en mémoire flash et retrouvées
après une coupure de courant. La librairie surveille les deux connexions et
se reconnecte seule en cas de perte.

## Dépendances

- Framework Arduino ESP32 (`WiFi`, `WebServer`, `DNSServer`, `Preferences`).
- [`knolleary/PubSubClient`](https://github.com/knolleary/pubsubclient) pour
  MQTT. À déclarer dans `platformio.ini` :

```ini
lib_deps = knolleary/PubSubClient@^2.8
```

## Fonctionnement

### WiFi

1. **Premier démarrage (pas d'identifiants en mémoire) :** l'ESP32 crée un
   point d'accès **ouvert** (sans mot de passe), nommé `ESP32-WifiAuto` par
   défaut. En s'y connectant avec un téléphone ou un ordinateur, la page de
   configuration s'ouvre automatiquement sur la plupart des téléphones, sinon
   elle est accessible à `http://192.168.1.1`.
2. **Saisie du réseau :** la page propose un champ SSID et un champ mot de
   passe (vide pour un réseau ouvert). Après validation, l'ESP32 enregistre
   ces identifiants en mémoire flash (NVS) et redémarre.
3. **Connexion :** l'ESP32 se connecte au réseau indiqué. Si les identifiants
   n'ont encore jamais fonctionné, plusieurs tentatives sont faites ; en cas
   d'échec de toutes, ils sont effacés et le portail de configuration se
   rouvre automatiquement. Une fois la connexion réussie une première fois,
   les identifiants sont considérés comme valides et ne sont plus effacés
   automatiquement.
4. **Démarrages suivants :** les identifiants enregistrés sont relus et
   utilisés directement, sans repasser par le portail.
5. **Reconnexion automatique :** si le signal WiFi est perdu, l'ESP32
   retente de se connecter périodiquement jusqu'au retour du réseau.
6. **Bouton de réinitialisation :** un appui maintenu 2 secondes (par défaut)
   sur un bouton (le bouton BOOT, GPIO 0, par défaut) efface les identifiants
   WiFi enregistrés et redémarre l'ESP32 en mode point d'accès. La
   configuration MQTT n'est pas concernée par ce bouton.

### MQTT

1. **Configuration :** une fois connecté à un réseau WiFi, la page
   `http://<IP de l'ESP32>/config` permet de saisir l'adresse du broker, son
   port, un identifiant client (optionnel) et des identifiants
   d'authentification (optionnels). Cette page **n'est jamais accessible en
   mode point d'accès** : tant que l'ESP32 n'a pas rejoint un réseau WiFi,
   elle répond « page indisponible ». La connexion au broker se fait en
   MQTT classique (non chiffré), la librairie n'embarque pas de client
   TLS/WebSocket.
2. **Statut affiché :** la page indique en permanence l'état de la
   connexion MQTT : aucune configuration enregistrée, connecté au broker
   (avec son adresse), ou non connecté (avec l'adresse et le motif de
   l'échec).
3. **Enregistrement :** les paramètres sont écrits en mémoire flash (NVS,
   dans un espace séparé de celui du WiFi) et appliqués immédiatement, sans
   redémarrer l'ESP32.
4. **Effacement :** un bouton sur la page `/config` ferme la connexion au
   broker et efface les paramètres MQTT de la mémoire, sans redémarrer
   l'ESP32.
5. **Connexion et reconnexion :** gérées automatiquement et de façon non
   bloquante, avec plusieurs tentatives espacées en cas d'échec. Tant qu'une
   configuration tout juste saisie n'a encore jamais réussi à se connecter,
   elle est considérée comme « non validée » : après 5 tentatives infructueuses
   (réglable), elle est jugée incorrecte (mauvaise adresse, mauvais
   identifiants...), automatiquement effacée de la mémoire, et les tentatives
   s'arrêtent — la page `/config` indique alors qu'il n'y a plus de
   configuration enregistrée. Une fois la connexion réussie une première
   fois, la configuration est considérée comme valide et n'est plus jamais
   effacée automatiquement : en cas de coupure du broker, l'ESP32 continue
   de retenter indéfiniment.
6. **Publication / souscription :** `mqttPublish()` envoie un message
   `String` sur un topic ; `mqttSubscribe()` s'abonne à un topic (avec
   wildcards `+`/`#`) et les messages reçus sont transmis au callback réglé
   par `setMqttMessageCallback()`, avec le topic et le payload.
7. **Statut de connexion régulier :** si un topic est fourni en 3ᵉ
   paramètre du constructeur, l'ESP32 y publie régulièrement `"online"`
   (retenu) tant qu'il est connecté au broker. Ce même topic sert
   automatiquement de testament MQTT (LWT) : si l'ESP32 se déconnecte
   brutalement (coupure de courant, perte du signal), le broker y publie
   lui-même `"offline"` (retenu).
8. **Logs :** chaque étape (connexion, échec, déconnexion avec motif,
   publication, souscription, enregistrement ou effacement de la
   configuration) est journalisée sur le port série, désactivable pour la
   production.

## Installation

Placer les fichiers `WifiAuto.h` et `WifiAuto.cpp` dans `lib/WifiAuto/` du
projet PlatformIO, et ajouter la dépendance `PubSubClient` (voir
ci-dessus).

## Utilisation minimale

```cpp
#include <Arduino.h>
#include "WifiAuto.h"

WifiAuto wifi;   // bouton BOOT (GPIO 0), point d'accès "ESP32-WifiAuto", pas de statut MQTT

void setup() {
  Serial.begin(115200);
  wifi.begin();
}

void loop() {
  wifi.update();   // à appeler à chaque tour de loop(), sans délai bloquant
}
```

## Utilisation avec MQTT

```cpp
#include <Arduino.h>
#include "WifiAuto.h"

// 3ᵉ paramètre : topic de statut, publié régulièrement en "online"/"offline" (retenu)
WifiAuto wifi(0, "ESP32-Salon", "esp32/salon/status");

void onMessage(const String& topic, const String& payload) {
  Serial.println("Recu sur " + topic + " : " + payload);
}

void setup() {
  Serial.begin(115200);

  wifi.setMqttMessageCallback(onMessage);

  // Se réabonner à chaque (re)connexion au broker : les souscriptions ne
  // sont pas mémorisées par la librairie, il faut les refaire ici
  wifi.onMqttConnected([]() {
    wifi.mqttSubscribe("esp32/salon/cmd");
  });

  wifi.begin();
}

void loop() {
  wifi.update();

  static uint32_t last = 0;
  if (wifi.isMqttConnected() && millis() - last >= 5000) {
    last = millis();
    wifi.mqttPublish("esp32/salon/temperature", String(21.5));
  }
}
```

Le broker (adresse, port, identifiants) se configure ensuite depuis
`http://<IP de l'ESP32>/config`, une fois l'ESP32 connecté au WiFi.

## Constructeur

```cpp
WifiAuto(uint8_t buttonPin = 0,
         const char* apName = "ESP32-WifiAuto",
         const char* mqttStatusTopic = nullptr);
```

| Paramètre         | Description                                                        | Défaut              |
|-------------------|----------------------------------------------------------------------|----------------------|
| `buttonPin`       | GPIO du bouton de réinitialisation WiFi                              | `0` (bouton BOOT)    |
| `apName`          | Nom (SSID) du point d'accès de configuration                         | `"ESP32-WifiAuto"`   |
| `mqttStatusTopic` | Topic de statut MQTT régulier (`"online"`/`"offline"`, retenu)       | `nullptr` (désactivé)|

```cpp
WifiAuto wifi(4, "MonESP32", "domotique/esp32-4/status");
```

> L'objet `WifiAuto` ne doit être déclaré qu'une seule fois (variable
> globale) et ne peut pas être copié. Cette limitation vient de
> `PubSubClient`, qui n'accepte pas de callback membre : une seule instance
> peut utiliser les fonctionnalités MQTT.

## Options WiFi (à régler avant `begin()`)

```cpp
WifiAuto wifi;

void setup() {
  Serial.begin(115200);

  // Bouton de réinitialisation sur une autre GPIO, actif à l'état haut,
  // sans résistance de pull-up interne (ex. bouton avec résistance externe)
  wifi.setButton(4, false, false);

  // Durée d'appui (ms) requise pour effacer la configuration WiFi (défaut : 2000)
  wifi.setResetHoldTime(3000);

  // Délai (ms) entre deux tentatives de reconnexion une fois le réseau validé
  // (défaut : 10000)
  wifi.setReconnectInterval(15000);

  // Politique de nouvelle tentative pour un réseau tout juste saisi (jamais
  // encore validé) : nombre de tentatives supplémentaires et durée (ms) de
  // chaque tentative. Si toutes échouent, les identifiants sont effacés et
  // le portail est rouvert. (défauts : 2 tentatives, 15000 ms chacune)
  wifi.setRetryPolicy(2, 15000);

  // Active (défaut) ou coupe les logs sur le port série (WiFi et MQTT).
  // Peut aussi être appelé à tout moment, y compris après begin()
  wifi.setDebug(false);

  // Adresse IP de l'ESP32 en mode point d'accès (défaut : 192.168.1.1)
  wifi.setPortalIP(IPAddress(10, 0, 0, 1));

  // Nom d'hôte de l'ESP32 sur le réseau une fois connecté
  wifi.setHostname("esp32-salon");

  // Fonctions appelées à la connexion et à la perte du WiFi
  wifi.onConnected([]() {
    Serial.println("WiFi connecte !");
  });
  wifi.onDisconnected([]() {
    Serial.println("WiFi perdu...");
  });

  wifi.begin();
}

void loop() {
  wifi.update();
}
```

### Détail des options WiFi

| Méthode | Rôle | Défaut |
|---|---|---|
| `setButton(pin, activeLow, usePullUp)` | GPIO du bouton, polarité et pull-up interne | GPIO 0, actif bas, pull-up activée |
| `setResetHoldTime(ms)` | Durée d'appui pour effacer la configuration WiFi | 2000 ms |
| `setReconnectInterval(ms)` | Délai entre deux tentatives de reconnexion (réseau déjà validé) | 10000 ms |
| `setRetryPolicy(retries, attemptTimeoutMs)` | Tentatives et délai avant retour au portail (réseau jamais validé) | 2 tentatives, 15000 ms |
| `setDebug(enabled)` | Active ou coupe les logs sur le port série (WiFi + MQTT) | activé |
| `setPortalIP(ip)` | Adresse IP de l'ESP32 en mode point d'accès | `192.168.1.1` |
| `setHostname(hostname)` | Nom d'hôte sur le réseau WiFi | (non défini) |
| `onConnected(callback)` | Fonction appelée à chaque connexion WiFi réussie | (aucune) |
| `onDisconnected(callback)` | Fonction appelée à chaque perte de connexion WiFi | (aucune) |

## Options MQTT (à régler avant `begin()`, sauf mention contraire)

```cpp
WifiAuto wifi(0, "ESP32-Salon", "esp32/salon/status");

void setup() {
  Serial.begin(115200);

  // Identifiants par défaut, écrasés si l'utilisateur en saisit via /config
  wifi.setMqttCredentials("monUser", "monMotDePasse");

  // Testament MQTT personnalisé (remplace le testament automatique basé sur
  // le topic de statut du constructeur)
  wifi.setMqttLastWill("esp32/salon/lwt", "disconnected", true, 1);

  // Callback appelé pour chaque message reçu sur un topic souscrit
  wifi.setMqttMessageCallback([](const String& topic, const String& payload) {
    Serial.println(topic + " = " + payload);
  });

  // Délai (ms) entre deux tentatives de connexion au broker (défaut : 5000)
  wifi.setMqttReconnectInterval(8000);

  // Intervalle (ms) de publication régulière du statut "online" (défaut : 60000)
  wifi.setMqttStatusInterval(30000);

  // Nombre de tentatives avant d'abandonner une configuration jamais validée et de
  // l'effacer de la mémoire (défaut : 5)
  wifi.setMqttMaxRetries(3);

  // Fonctions appelées à la connexion / déconnexion du broker MQTT
  wifi.onMqttConnected([]() {
    wifi.mqttSubscribe("esp32/salon/cmd");
  });
  wifi.onMqttDisconnected([]() {
    Serial.println("MQTT perdu...");
  });

  wifi.begin();
}

void loop() {
  wifi.update();
}
```

### Détail des options MQTT

| Méthode | Rôle | Défaut |
|---|---|---|
| `setMqttCredentials(user, pass)` | Identifiants par défaut (écrasés par ceux saisis via `/config`) | (aucun) |
| `setMqttLastWill(topic, message, retain, qos)` | Testament MQTT personnalisé, prioritaire sur le testament automatique | (testament automatique sur le topic de statut) |
| `setMqttMessageCallback(callback)` | Fonction appelée pour chaque message reçu (topic, payload) | (aucune) |
| `setMqttReconnectInterval(ms)` | Délai entre deux tentatives de connexion au broker | 5000 ms |
| `setMqttStatusInterval(ms)` | Intervalle de publication régulière du statut `"online"` | 60000 ms |
| `setMqttMaxRetries(retries)` | Tentatives avant d'effacer une configuration jamais validée | 5 |
| `onMqttConnected(callback)` | Fonction appelée à chaque connexion réussie au broker | (aucune) |
| `onMqttDisconnected(callback)` | Fonction appelée à chaque perte de connexion au broker | (aucune) |

## Méthodes principales

| Méthode | Rôle |
|---|---|
| `begin()` | Démarre la librairie (à appeler dans `setup()`) |
| `update()` | Gère le bouton, le portail, la reconnexion WiFi et MQTT (à appeler dans `loop()`, sans délai bloquant) |
| `clearCredentials()` | Efface le SSID et le mot de passe WiFi en mémoire, sans redémarrer |
| `resetAndRestart()` | Efface la configuration WiFi puis redémarre en mode point d'accès (équivalent à l'appui long sur le bouton) |
| `clearMqttConfig()` | Ferme la connexion MQTT et efface sa configuration en mémoire, sans redémarrer |

## MQTT : publier, souscrire

```cpp
// Publier un message (retain et qos optionnels ; qos est ignoré par PubSubClient,
// qui ne publie qu'en QoS 0, conservé pour compatibilité d'API)
wifi.mqttPublish("esp32/salon/temperature", String(21.5));
wifi.mqttPublish("esp32/salon/etat", "ON", true);

// Souscrire / se désabonner (wildcards + et # supportés)
wifi.mqttSubscribe("esp32/salon/cmd");
wifi.mqttUnsubscribe("esp32/salon/cmd");
```

> Les souscriptions ne sont pas mémorisées par la librairie : après chaque
> reconnexion au broker, il faut les refaire, par exemple dans le callback
> `onMqttConnected()`.

## État

```cpp
// WiFi
if (wifi.isConnected()) {
  Serial.println(wifi.getLocalIP());
}
if (wifi.isPortalActive()) {
  Serial.println("En attente de configuration...");
}
Serial.println(wifi.hasCredentials() ? "Identifiants en memoire" : "Aucun identifiant");
Serial.println(wifi.getSSID());
Serial.printf("Dernier motif de deconnexion WiFi : %u (%s)\n",
              wifi.getLastDisconnectReason(),
              wifi.getLastDisconnectReasonText().c_str());

// MQTT
Serial.println(wifi.hasMqttConfig() ? "Broker MQTT configure" : "Aucun broker configure");
Serial.println(wifi.isMqttConnected() ? "Connecte au broker" : "Non connecte");
Serial.println(wifi.getMqttHost());
Serial.println(wifi.getMqttPort());
Serial.printf("Etat MQTT : %d (%s)\n", wifi.getMqttStateCode(), wifi.getMqttStateText().c_str());
```

| Méthode | Rôle |
|---|---|
| `isConnected()` | `true` si connecté au réseau WiFi |
| `isPortalActive()` | `true` en mode point d'accès / portail de configuration |
| `hasCredentials()` | `true` si un SSID est enregistré en mémoire |
| `getSSID()` | SSID actuellement enregistré |
| `getLocalIP()` | IP obtenue sur le réseau (ou IP du portail en mode point d'accès) |
| `getLastDisconnectReason()` | Code du dernier motif de déconnexion WiFi (ESP-IDF, `0` = aucun) |
| `getLastDisconnectReasonText()` | Description en français du dernier motif de déconnexion WiFi |
| `hasMqttConfig()` | `true` si un broker MQTT est enregistré en mémoire |
| `isMqttConnected()` | `true` si connecté au broker MQTT |
| `getMqttHost()` / `getMqttPort()` | Adresse et port du broker enregistré |
| `getMqttStateCode()` / `getMqttStateText()` | Code d'état PubSubClient et sa description |

## Réinitialiser la configuration WiFi

Deux façons équivalentes :

- **Matériel :** maintenir le bouton configuré (BOOT par défaut) pendant la
  durée réglée par `setResetHoldTime()` (2 s par défaut).
- **Logiciel :** appeler `wifi.resetAndRestart();` depuis le code.

Dans les deux cas, le SSID et le mot de passe WiFi sont effacés de la
mémoire flash et l'ESP32 redémarre en mode point d'accès. La configuration
MQTT n'est pas affectée.

## Réinitialiser la configuration MQTT

- **Page web :** le bouton « Effacer la configuration MQTT » sur
  `http://<IP>/config`.
- **Logiciel :** appeler `wifi.clearMqttConfig();` depuis le code.

Dans les deux cas, la connexion au broker est fermée et ses paramètres sont
effacés de la mémoire flash, sans redémarrer l'ESP32.

## Logs sur le port série

Avec `Serial.begin(...)` actif, chaque étape est journalisée avec le
préfixe `[WifiAuto]` :

- **WiFi :** démarrage du portail, connexion d'un client au portail,
  tentative de connexion, connexion réussie (avec IP), perte de signal
  (avec motif), nouvelle tentative, retour au portail, effacement des
  identifiants, redémarrage. Les événements bas niveau du driver WiFi
  (association, IP obtenue, déconnexion avec motif) sont journalisés,
  préfixés par `[evt]`.
- **MQTT :** tentative de connexion, connexion réussie, échec (avec le
  code d'état et sa description), perte de connexion, publication d'un
  message, souscription, enregistrement ou effacement de la configuration
  (préfixés par `[MQTT]`).

`wifi.setDebug(false);` coupe l'ensemble de ces logs (WiFi et MQTT), par
exemple pour une version de production.

## Remarques

- Le bouton BOOT (GPIO 0) ne doit pas être maintenu enfoncé pendant la mise
  sous tension de l'ESP32, sous peine de démarrer en mode téléchargement du
  firmware.
- Si le mot de passe WiFi saisi est refusé après toutes les tentatives, le
  portail se rouvre automatiquement : il n'est pas nécessaire d'utiliser le
  bouton dans ce cas.
- La page `/config` (MQTT) n'est jamais servie en mode point d'accès : elle
  n'a de sens qu'une fois l'ESP32 connecté à un réseau WiFi existant, avec
  un accès potentiel à un broker MQTT.
- Sur la page `/config`, laisser le champ mot de passe vide en enregistrant
  conserve le mot de passe déjà enregistré ; pour l'effacer, utiliser le
  bouton « Effacer la configuration MQTT ».
- Une alimentation insuffisante (câble ou port USB faible) peut déclencher
  le détecteur de sous-tension de l'ESP32 (`Brownout detector was
  triggered`) au démarrage du WiFi. Ce n'est pas lié à la librairie : il
  faut alors changer de câble, de port USB, ou d'alimentation.
