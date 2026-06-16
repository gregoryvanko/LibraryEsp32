#include "CANManager.h"

/**
 * @file CANManager.cpp
 * @brief Implémentation du gestionnaire CAN pour ESP32
 */

CANManager::CANManager() : status(CAN_UNINITIALIZED) {
    // Configuration par défaut du TWAI (CAN du ESP32)
    generalConfig = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)17, (gpio_num_t)16, TWAI_MODE_NORMAL);
    
    // Configuration temporelle pour 1 Mbps
    timingConfig = TWAI_TIMING_CONFIG_1MBITS();
    
    // Pas de filtrage (accepte tous les messages)
    filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();
}

bool CANManager::begin(int rxPin, int txPin, uint32_t baudrate) {
    // Reconfigure les broches
    generalConfig.tx_io = (gpio_num_t)txPin;
    generalConfig.rx_io = (gpio_num_t)rxPin;

    // Configure la vitesse de transmission
    switch (baudrate) {
        case 250000:
            timingConfig = TWAI_TIMING_CONFIG_250KBITS();
            break;
        case 500000:
            timingConfig = TWAI_TIMING_CONFIG_500KBITS();
            break;
        case 1000000:
        default:
            timingConfig = TWAI_TIMING_CONFIG_1MBITS();
            break;
    }

    // Installe le pilote TWAI
    if (twai_driver_install(&generalConfig, &timingConfig, &filterConfig) != ESP_OK) {
        status = CAN_ERROR;
        Serial.printf("[CANManager] Erreur twai_driver_install\n");
        return false;
    }

    // Démarre le pilote TWAI
    if (twai_start() != ESP_OK) {
        status = CAN_ERROR;
        twai_driver_uninstall();
        Serial.printf("[CANManager] Erreur twai_start\n");
        return false;
    }

    status = CAN_RUNNING;

    Serial.printf("[CANManager] initialisé\n");

    return true;
}

bool CANManager::end() {
    if (status == CAN_UNINITIALIZED) {
        return false;
    }

    if (twai_stop() != ESP_OK) {
        status = CAN_ERROR;
        return false;
    }

    if (twai_driver_uninstall() != ESP_OK) {
        status = CAN_ERROR;
        return false;
    }

    status = CAN_UNINITIALIZED;
    return true;
}

bool CANManager::sendBool(uint32_t id, bool value) {
    if (status != CAN_RUNNING) {
        return false;
    }

    twai_message_t message = {};
    message.identifier = id;
    message.data_length_code = 1;
    message.data[0] = value ? 0x01 : 0x00;

    esp_err_t result = twai_transmit(&message, pdMS_TO_TICKS(10));

    if (result != ESP_OK){
        Serial.printf("[CANManager] Erreur envoi du Bool: %s\n" , esp_err_to_name(result));
    }
    
    return result == ESP_OK;
}

bool CANManager::sendFloat(uint32_t id, float value) {
    if (status != CAN_RUNNING) {
        return false;
    }

    twai_message_t message = {};
    message.identifier = id;
    message.data_length_code = 4;
    
    floatToBytes(value, message.data);

    esp_err_t result = twai_transmit(&message, pdMS_TO_TICKS(10));

    if (result != ESP_OK){
        Serial.printf("[CANManager] Erreur envoi du Float: %s\n" , esp_err_to_name(result));
    }

    return result == ESP_OK;
}

bool CANManager::sendInt16(uint32_t id, int16_t value) {
    if (status != CAN_RUNNING) {
        return false;
    }
 
    twai_message_t message = {};
    message.identifier = id;
    message.data_length_code = 2;
    
    int16ToBytes(value, message.data);
 
    esp_err_t result = twai_transmit(&message, pdMS_TO_TICKS(10));

    if (result != ESP_OK){
        Serial.printf("[CANManager] Erreur envoi du Int16: %s\n" , esp_err_to_name(result));
    }

    return result == ESP_OK;
}
 
bool CANManager::sendInt32(uint32_t id, int32_t value) {
    if (status != CAN_RUNNING) {
        return false;
    }
 
    twai_message_t message = {};
    message.identifier = id;
    message.data_length_code = 4;
    
    int32ToBytes(value, message.data);
 
    esp_err_t result = twai_transmit(&message, pdMS_TO_TICKS(10));

    if (result != ESP_OK){
        Serial.printf("[CANManager] Erreur envoi du Int32: %s\n" , esp_err_to_name(result));
    }

    return result == ESP_OK;
}

bool CANManager::receive(CANMessage &message) {
    if (status != CAN_RUNNING) {
        Serial.printf("[CANManager] Erreur CAN not Running\n");
        return false;
    }

    twai_message_t rxMessage;
    esp_err_t result = twai_receive(&rxMessage, 0);

    if (result != ESP_OK) {
        return false;
    }

    message.id = rxMessage.identifier;
    message.dlc = rxMessage.data_length_code;
    
    for (int i = 0; i < 8; i++) {
        message.data[i] = rxMessage.data[i];
    }

    // Détermine le type de données en fonction de la longueur
    // Note: DATA_INT16 et DATA_INT32 sont déterminés par convention
    // car ils partagent la même taille que d'autres types
    if (message.dlc == 1) {
        message.type = DATA_BOOL;
    } else if (message.dlc == 2) {
        message.type = DATA_INT16;
    } else if (message.dlc == 4) {
        message.type = DATA_INT32;  // Par défaut, 4 bytes = INT32
    } else {
        message.type = DATA_UNKNOWN;
    }

    return true;
}

CANManager::CANStatus CANManager::getStatus() const {
    return status;
}

const char* CANManager::getStatusString() const {
    switch (status) {
        case CAN_UNINITIALIZED:
            return "Non initialisé";
        case CAN_INITIALIZED:
            return "Initialisé";
        case CAN_RUNNING:
            return "En cours d'exécution";
        case CAN_ERROR:
            return "Erreur";
        default:
            return "État inconnu";
    }
}

bool CANManager::decodeBool(const CANMessage &message) {
    if (message.dlc < 1) {
        return false;
    }
    return message.data[0] != 0x00;
}

float CANManager::decodeFloat(const CANMessage &message) {
    if (message.dlc < 4) {
        return 0.0f;
    }
    return bytesToFloat(message.data);
}

int16_t CANManager::decodeInt16(const CANMessage &message) {
    if (message.dlc < 2) {
        return 0;
    }
    return bytesToInt16(message.data);
}
 
int32_t CANManager::decodeInt32(const CANMessage &message) {
    if (message.dlc < 4) {
        return 0;
    }
    return bytesToInt32(message.data);
}

bool CANManager::setFilterRange(uint32_t idStart, uint32_t idEnd, bool useExtendedId) {
    if (idStart > idEnd) {
        Serial.printf("[CANManager] Erreur set filter range : Plage invalide\n");
        return false;  // Plage invalide
    }
 
    // Calcule le code d'acceptation et le masque pour couvrir la plage
    // Trouver les bits qui varient entre idStart et idEnd
    uint32_t diff = idStart ^ idEnd;
    
    // Trouver le bit de poids faible qui varie
    uint32_t mask = 0xFFFFFFFF;
    for (int i = 0; i < 32; i++) {
        if (diff & (1 << i)) {
            // Tous les bits à partir de ce bit sont variables
            mask = ~((1 << (i + 1)) - 1);
            break;
        }
    }
 
    // Le code d'acceptation peut être soit idStart soit idEnd (les bits constants sont les mêmes)
    uint32_t acceptanceCode = idStart & mask;
    
    // Décaler pour le format du registre TWAI (11-bit: décalage de 21, 29-bit: décalage de 0)
    if (!useExtendedId) {
        acceptanceCode <<= 21;
        mask <<= 21;
    }
 
    // Inverser le masque (1 = ignorer le bit, 0 = comparer)
    mask = ~mask;
 
    // Appliquer au filtre global
    filterConfig.acceptance_code = acceptanceCode;
    filterConfig.acceptance_mask = mask;
    filterConfig.single_filter = true;
 
    return true;
}

bool CANManager::setFilterMask(uint32_t id, uint32_t mask, bool useExtendedId) {
    if (status != CAN_UNINITIALIZED) {
        // Le filtre ne peut être configuré que avant l'initialisation
        return false;
    }
 
    // Décaler pour le format du registre TWAI si IDs 11-bit
    uint32_t shiftedId = id;
    uint32_t shiftedMask = mask;
    
    if (!useExtendedId) {
        shiftedId <<= 21;
        shiftedMask <<= 21;
    }
 
    // Inverser le masque (convention TWAI: 1 = ignorer, 0 = comparer)
    shiftedMask = ~shiftedMask;
 
    filterConfig.acceptance_code = shiftedId;
    filterConfig.acceptance_mask = shiftedMask;
    filterConfig.single_filter = true;
 
    return true;
}

void CANManager::floatToBytes(float value, uint8_t *bytes) {
    // Interprète le float comme un tableau de 4 bytes (IEEE 754)
    uint8_t *ptr = (uint8_t *)&value;
    bytes[0] = ptr[0];
    bytes[1] = ptr[1];
    bytes[2] = ptr[2];
    bytes[3] = ptr[3];
}

float CANManager::bytesToFloat(const uint8_t *bytes) {
    // Reconstruit le float à partir de 4 bytes
    float value;
    uint8_t *ptr = (uint8_t *)&value;
    ptr[0] = bytes[0];
    ptr[1] = bytes[1];
    ptr[2] = bytes[2];
    ptr[3] = bytes[3];
    return value;
}

void CANManager::int16ToBytes(int16_t value, uint8_t *bytes) {
    // Convertit un int16_t en big-endian (MSB en premier)
    bytes[0] = (value >> 8) & 0xFF;
    bytes[1] = value & 0xFF;
}
 
int16_t CANManager::bytesToInt16(const uint8_t *bytes) {
    // Reconstruit un int16_t depuis big-endian
    return (int16_t)((bytes[0] << 8) | bytes[1]);
}
 
void CANManager::int32ToBytes(int32_t value, uint8_t *bytes) {
    // Convertit un int32_t en big-endian (MSB en premier)
    bytes[0] = (value >> 24) & 0xFF;
    bytes[1] = (value >> 16) & 0xFF;
    bytes[2] = (value >> 8) & 0xFF;
    bytes[3] = value & 0xFF;
}
 
int32_t CANManager::bytesToInt32(const uint8_t *bytes) {
    // Reconstruit un int32_t depuis big-endian
    return (int32_t)((bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3]);
}