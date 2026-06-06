// =============================================================================
//  espnow_comm.cpp
//  Module de communication ESP-NOW pour le projet hydroponique
// =============================================================================

#include "espnow_comm.h"

// -----------------------------------------------------------------------------
// Initialisation des membres statiques
// -----------------------------------------------------------------------------
volatile bool      ESPNowComm::_newMessageFlag  = false;
espnow_message_t   ESPNowComm::_receivedMsg     = {};
uint8_t            ESPNowComm::_senderMac[6]    = {0};
bool               ESPNowComm::_lastSendSuccess = false;

// Instance globale accessible depuis les .ino / autres .cpp
ESPNowComm espnowComm;

// =============================================================================
// Constructeur
// =============================================================================
ESPNowComm::ESPNowComm()
    : _initialized(false)
    , _preserveWiFi(false)
    , _hasPeer(false)
{
    memset(_peerMac, 0, 6);
}

// =============================================================================
// begin() — Initialisation Wi-Fi + ESP-NOW
// =============================================================================
bool ESPNowComm::begin(const uint8_t* peerMac, bool preserveWiFi)
{
    _preserveWiFi = preserveWiFi;

    // 1. Configuration Wi-Fi
    if (preserveWiFi) {
        // L'ESP32 est déjà connecté au routeur — on ne touche à rien.
        // ESP-NOW coexiste nativement avec une connexion STA active.
        Serial.println("[ESPNow] Wi-Fi conservé (connexion routeur maintenue)");

        // Vérification de cohérence : le mode doit être STA ou APSTA
        wifi_mode_t mode;
        esp_wifi_get_mode(&mode);
        if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
            Serial.println("[ESPNow] AVERTISSEMENT : mode Wi-Fi inattendu pour ESP-NOW");
        }
    } else {
        // Comportement original : STA sans connexion routeur
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        Serial.println("[ESPNow] Wi-Fi configuré en mode STA (sans routeur)");
        // ── Forcer le canal radio AVANT esp_now_init() ──────────────────
        esp_wifi_start();                                      // démarre le driver si pas encore fait
        esp_wifi_set_promiscuous(true);                        // nécessaire pour changer de canal sans être associé
        esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(false);

        uint8_t ch; wifi_second_chan_t sec;
        esp_wifi_get_channel(&ch, &sec);
        Serial.printf("[ESPNow] Canal radio forcé : %d\n", ch);
    }

    // 2. Initialisation ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNow] ERREUR : esp_now_init() a échoué");
        return false;
    }

    // 3. Enregistrement des callbacks
    esp_now_register_send_cb(ESPNowComm::onDataSent);
    esp_now_register_recv_cb(ESPNowComm::onDataReceived);

    // 4. Enregistrement du pair (si adresse fournie — côté émetteur)
    if (peerMac != nullptr) {
        memcpy(_peerMac, peerMac, 6);
        if (!_registerPeer(peerMac)) {
            return false;
        }
        _hasPeer = true;
    }

    _initialized = true;
    Serial.println("[ESPNow] Initialisation réussie");
    printMacAddress();
    return true;
}

// =============================================================================
// sendMessage() — Envoi d'un message String
// =============================================================================
bool ESPNowComm::sendMessage(const String& message)
{
    if (!_initialized || !_hasPeer) {
        Serial.println("[ESPNow] ERREUR : non initialisé ou pas de pair enregistré");
        return false;
    }

    // Construction du paquet
    espnow_message_t msg;
    msg.timestamp_ms = (uint32_t)millis();

    // Copie sécurisée avec troncature si nécessaire
    strncpy(msg.payload, message.c_str(), ESPNOW_MAX_STR_LEN - 1);
    msg.payload[ESPNOW_MAX_STR_LEN - 1] = '\0';

    if (message.length() >= ESPNOW_MAX_STR_LEN) {
        Serial.printf("[ESPNow] AVERTISSEMENT : message tronqué à %d caractères\n",
                      ESPNOW_MAX_STR_LEN - 1);
    }

    // Envoi
    esp_err_t result = esp_now_send(_peerMac,
                                    (const uint8_t*)&msg,
                                    sizeof(espnow_message_t));

    if (result != ESP_OK) {
        Serial.printf("[ESPNow] ERREUR esp_now_send() : %s\n", esp_err_to_name(result));
        return false;
    }

    Serial.printf("[ESPNow] Message envoyé (%d octets) : \"%s\"\n",
                  sizeof(espnow_message_t), msg.payload);
    return true;
}

// =============================================================================
// Accesseurs — message reçu
// =============================================================================

bool ESPNowComm::hasNewMessage() const
{
    return _newMessageFlag;
}

String ESPNowComm::getLastMessage()
{
    _newMessageFlag = false;                     // Acquittement
    return String(_receivedMsg.payload);
}

uint32_t ESPNowComm::getLastMessageTimestamp() const
{
    return _receivedMsg.timestamp_ms;
}

uint8_t* ESPNowComm::getLastSenderMac()
{
    return _senderMac;
}

// =============================================================================
// printMacAddress() — Affiche l'adresse MAC dans le moniteur série
// =============================================================================
void ESPNowComm::printMacAddress()
{
    Serial.print("[ESPNow] Adresse MAC de cet ESP32 : ");
    Serial.println(WiFi.macAddress());
}

// =============================================================================
// _registerPeer() — Enregistre un pair ESP-NOW
// =============================================================================
bool ESPNowComm::_registerPeer(const uint8_t* mac)
{
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = _preserveWiFi ? (uint8_t)WiFi.channel() : ESPNOW_CHANNEL;
    peerInfo.encrypt  = false;            // Chiffrement désactivé (à activer si besoin)
    peerInfo.ifidx    = WIFI_IF_STA;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESPNow] ERREUR : impossible d'enregistrer le pair");
        return false;
    }

    Serial.printf("[ESPNow] Pair enregistré : %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

// =============================================================================
// Callback statique — Confirmation d'envoi
// =============================================================================
void ESPNowComm::onDataSent(const esp_now_send_info_t* sendInfo, esp_now_send_status_t status)
{
    _lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
    Serial.printf("[ESPNow] Accusé de réception vers %02X:%02X:%02X:%02X:%02X:%02X : %s\n",
                  sendInfo->des_addr[0], sendInfo->des_addr[1], sendInfo->des_addr[2],
                  sendInfo->des_addr[3], sendInfo->des_addr[4], sendInfo->des_addr[5],
                  _lastSendSuccess ? "OK" : "ÉCHEC");
}

// =============================================================================
// Callback statique — Réception d'un message
// =============================================================================
void ESPNowComm::onDataReceived(const esp_now_recv_info_t* recvInfo, const uint8_t* data, int len)
{
    if (len != sizeof(espnow_message_t)) {
        Serial.printf("[ESPNow] ERREUR : taille reçue incorrecte (%d octets attendus, %d reçus)\n",
                      sizeof(espnow_message_t), len);
        return;
    }

    // Copie des données dans le buffer statique
    memcpy(&_receivedMsg, data, sizeof(espnow_message_t));
    memcpy(_senderMac, recvInfo->src_addr, 6);
    _newMessageFlag = true;

    Serial.printf("[ESPNow] Message reçu de %02X:%02X:%02X:%02X:%02X:%02X : \"%s\"\n",
                  recvInfo->src_addr[0], recvInfo->src_addr[1], recvInfo->src_addr[2],
                  recvInfo->src_addr[3], recvInfo->src_addr[4], recvInfo->src_addr[5],
                  _receivedMsg.payload);
}