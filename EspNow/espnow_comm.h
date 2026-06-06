#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h> 

// =============================================================================
//  espnow_comm.h
//  Module de communication ESP-NOW pour le projet hydroponique
//  Échange de messages de type String entre deux ESP32
// =============================================================================

// --- Constantes ---------------------------------------------------------------

#define ESPNOW_CHANNEL       6          // Canal Wi-Fi utilisé par ESP-NOW
#define ESPNOW_MAX_STR_LEN   240        // Longueur max de la chaîne (limite ESP-NOW : 250 octets)

// --- Structure du message -----------------------------------------------------

/**
 * @brief Structure de données échangée via ESP-NOW.
 *        Doit être identique sur l'émetteur ET le récepteur.
 */
typedef struct espnow_message_t {
    char     payload[ESPNOW_MAX_STR_LEN]; // Contenu texte du message
    uint32_t timestamp_ms;               // Horodatage (millis() côté émetteur)
} espnow_message_t;

// --- Classe principale --------------------------------------------------------

class ESPNowComm {
public:
    // -------------------------------------------------------------------------
    // Constructeur
    // -------------------------------------------------------------------------
    ESPNowComm();

    // -------------------------------------------------------------------------
    // Initialisation
    // Doit être appelée dans setup(), APRÈS Serial.begin()
    // @param peerMac  Adresse MAC du pair (tableau 6 octets).
    //                 Passer nullptr sur le récepteur (pas de pair à enregistrer).
    // @param preserveWiFi  true  → Wi-Fi déjà configuré/connecté, begin() n'y touche pas.
    //                      false → begin() configure le Wi-Fi en mode STA sans routeur (comportement original).
    // @return true si l'initialisation a réussi
    // -------------------------------------------------------------------------
    bool begin(const uint8_t* peerMac = nullptr, bool preserveWiFi = false);

    // -------------------------------------------------------------------------
    // Envoi d'un message String
    // @param message  Chaîne à envoyer (tronquée si > ESPNOW_MAX_STR_LEN-1)
    // @return true si la transmission a été acceptée par la couche MAC
    // -------------------------------------------------------------------------
    bool sendMessage(const String& message);

    // -------------------------------------------------------------------------
    // Accesseurs pour le dernier message reçu
    // -------------------------------------------------------------------------
    bool          hasNewMessage() const;           // true s'il y a un msg non lu
    String        getLastMessage();                // Retourne le msg et efface le flag
    uint32_t      getLastMessageTimestamp() const;
    uint8_t*      getLastSenderMac();              // Adresse MAC de l'émetteur

    // -------------------------------------------------------------------------
    // Affiche l'adresse MAC de cet ESP32 (utile pour configurer le pair)
    // -------------------------------------------------------------------------
    static void printMacAddress();

    // -------------------------------------------------------------------------
    // Callbacks ESP-NOW (appelés par le framework, ne pas appeler directement)
    // -------------------------------------------------------------------------
    static void onDataSent(const esp_now_send_info_t* sendInfo, esp_now_send_status_t status);
    static void onDataReceived(const esp_now_recv_info_t* recvInfo, const uint8_t* data, int len);

private:
    bool              _initialized;
    bool              _preserveWiFi;
    uint8_t           _peerMac[6];
    bool              _hasPeer;

    // Données du dernier message reçu (accédées depuis le callback statique)
    static volatile bool          _newMessageFlag;
    static espnow_message_t       _receivedMsg;
    static uint8_t                _senderMac[6];
    static bool                   _lastSendSuccess;

    bool _registerPeer(const uint8_t* mac);
};

// Instance globale unique (pattern singleton léger)
// Déclarée dans le .cpp — inclure ce header suffit pour y accéder
extern ESPNowComm espnowComm;