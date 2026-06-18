#pragma once
#include <Arduino.h>
#include <driver/twai.h>

/**
 * @file CANManager.h
 * @brief Gestionnaire CAN pour ESP32 avec module SN65HVD230
 * @details Permet l'envoi et la réception de messages CAN contenant des booleans et des floats
 */

class CANManager {
public:
    /**
     * @brief Énumération des états possibles du gestionnaire CAN
     */
    enum CANStatus {
        CAN_UNINITIALIZED,
        CAN_INITIALIZED,
        CAN_RUNNING,
        CAN_ERROR
    };

    /**
     * @brief Énumération des types de données supportées
     */
    enum DataType {
        DATA_BOOL,
        DATA_FLOAT,
        DATA_INT16,
        DATA_INT32,
        DATA_UNKNOWN
    };

    /**
     * @brief Structure pour recevoir les messages CAN
     */
    struct CANMessage {
        uint32_t id;
        uint8_t dlc;
        uint8_t data[8];
        DataType type;
    };

    /**
     * @brief Constructeur par défaut
     */
    CANManager();

    /**
     * @brief Initialise le bus CAN
     * @param rxPin Broche de réception (GPIO16 pour ESP32 par défaut)
     * @param txPin Broche de transmission (GPIO17 pour ESP32 par défaut)
     * @param baudrate Vitesse en bauds (1000 kbps par défaut)
     * @return true si l'initialisation a réussi, false sinon
     */
    bool begin(int rxPin = 16, int txPin = 17, uint32_t baudrate = 1000000);

    /**
     * @brief Arrête le bus CAN
     * @return true si l'arrêt a réussi, false sinon
     */
    bool end();

    /**
     * @brief Envoie une valeur booléenne sur le bus CAN
     * @param id Identifiant du message CAN (11 bits ou 29 bits)
     * @param value Valeur booléenne à envoyer (true ou false)
     * @return true si l'envoi a réussi, false sinon
     */
    bool sendBool(uint32_t id, bool value);

    /**
     * @brief Envoie une valeur flottante sur le bus CAN
     * @param id Identifiant du message CAN (11 bits ou 29 bits)
     * @param value Valeur float à envoyer
     * @return true si l'envoi a réussi, false sinon
     */
    bool sendFloat(uint32_t id, float value);

    /**
     * @brief Envoie une valeur entière 16 bits signée sur le bus CAN
     * @param id Identifiant du message CAN (11 bits ou 29 bits)
     * @param value Valeur int16_t à envoyer (-32768 à 32767)
     * @return true si l'envoi a réussi, false sinon
     */
    bool sendInt16(uint32_t id, int16_t value);
 
    /**
     * @brief Envoie une valeur entière 32 bits signée sur le bus CAN
     * @param id Identifiant du message CAN (11 bits ou 29 bits)
     * @param value Valeur int32_t à envoyer
     * @return true si l'envoi a réussi, false sinon
     */
    bool sendInt32(uint32_t id, int32_t value);

    /**
     * @brief Reçoit un message du bus CAN (non bloquant)
     * @param message Structure où stocker le message reçu
     * @return true si un message a été reçu, false sinon
     */
    bool receive(CANMessage &message);

    /**
     * @brief Obtient l'état actuel du gestionnaire CAN
     * @return État du gestionnaire
     */
    CANStatus getStatus() const;

    /**
     * @brief Retourne une description textuelle de l'état
     * @return Chaîne de caractères décrivant l'état
     */
    const char* getStatusString() const;

    /**
     * @brief Décodifie un message CAN contenant un booléen
     * @param message Message CAN reçu
     * @return Valeur booléenne décodée
     */
    static bool decodeBool(const CANMessage &message);

    /**
     * @brief Décodifie un message CAN contenant un float
     * @param message Message CAN reçu
     * @return Valeur float décodée
     */
    static float decodeFloat(const CANMessage &message);

    /**
     * @brief Décodifie un message CAN contenant un entier 16 bits
     * @param message Message CAN reçu
     * @return Valeur int16_t décodée
     */
    static int16_t decodeInt16(const CANMessage &message);
 
    /**
     * @brief Décodifie un message CAN contenant un entier 32 bits
     * @param message Message CAN reçu
     * @return Valeur int32_t décodée
     */
    static int32_t decodeInt32(const CANMessage &message);

    /**
     * @brief Configure un filtre hardware pour accepter un range continu d'IDs CAN
     * @details Utilise les registres de filtre hardware du TWAI pour une efficacité maximale.
     *          Permet de filtrer les messages dont l'ID est dans une plage donnée.
     *          NOTE: Doit être appelé AVANT begin()
     * @param idStart ID de début de la plage (inclus)
     * @param idEnd ID de fin de la plage (inclus)
     * @param useExtendedId true pour les identifiants 29 bits, false pour 11 bits (défaut)
     * @return true si la configuration a réussi, false sinon
     * 
     * @example
     * can.setFilterRange(0x100, 0x10F);  // Accepte les IDs de 0x100 à 0x10F
     * can.begin();
     */
    bool setFilterRange(uint32_t idStart, uint32_t idEnd, bool useExtendedId = false);
 
    /**
     * @brief Configure un filtre hardware pour accepter un ID unique avec masque personnalisé
     * @details Version avancée pour un contrôle fin du filtrage hardware
     *          NOTE: Doit être appelé AVANT begin()
     * @param id ID du message à accepter
     * @param mask Masque de filtrage (1 = ignorer le bit, 0 = vérifier le bit)
     * @param useExtendedId true pour les identifiants 29 bits, false pour 11 bits (défaut)
     * @return true si la configuration a réussi
     * 
     * @example
     * // Accepter les IDs dont les bits 3-0 varient librement
     * can.setFilterMask(0x100, 0x7F0);
     * can.begin();
     */
    bool setFilterMask(uint32_t id, uint32_t mask, bool useExtendedId = false);

private:
    CANStatus status;
    twai_general_config_t generalConfig;
    twai_timing_config_t timingConfig;
    twai_filter_config_t filterConfig;

    /**
     * @brief Convertit un float en tableau de 4 bytes (IEEE 754)
     * @param value Valeur float
     * @param bytes Tableau de 4 bytes (sortie)
     */
    static void floatToBytes(float value, uint8_t *bytes);

    /**
     * @brief Convertit un tableau de 4 bytes en float (IEEE 754)
     * @param bytes Tableau de 4 bytes
     * @return Valeur float
     */
    static float bytesToFloat(const uint8_t *bytes);

    /**
     * @brief Convertit un int16_t en tableau de 2 bytes (big-endian)
     * @param value Valeur int16_t
     * @param bytes Tableau de 2 bytes (sortie)
     */
    static void int16ToBytes(int16_t value, uint8_t *bytes);
 
    /**
     * @brief Convertit un tableau de 2 bytes en int16_t (big-endian)
     * @param bytes Tableau de 2 bytes
     * @return Valeur int16_t
     */
    static int16_t bytesToInt16(const uint8_t *bytes);
 
    /**
     * @brief Convertit un int32_t en tableau de 4 bytes (big-endian)
     * @param value Valeur int32_t
     * @param bytes Tableau de 4 bytes (sortie)
     */
    static void int32ToBytes(int32_t value, uint8_t *bytes);
 
    /**
     * @brief Convertit un tableau de 4 bytes en int32_t (big-endian)
     * @param bytes Tableau de 4 bytes
     * @return Valeur int32_t
     */
    static int32_t bytesToInt32(const uint8_t *bytes);
};
