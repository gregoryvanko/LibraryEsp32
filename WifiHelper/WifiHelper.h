#pragma once

// Délai minimum entre deux tentatives de reconnexion (ms)
const unsigned long WIFI_RECONNECT_INTERVAL = 5000;
unsigned long _lastReconnectAttempt = 0;

char* WIFI_SSI_Saved     = "";
char* WIFI_PASSWORD_Saved = "";

void connectToWiFi(char* WIFI_SSID, char* WIFI_PASSWORD) {
    WIFI_SSI_Saved = WIFI_SSID;
    WIFI_PASSWORD_Saved = WIFI_PASSWORD;
    Serial.printf("[WifiHelper] Connexion à \"%s\"", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\n[WifiHelper] Connecté — IP : %s  Canal : %d\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.channel());
}

void handleWiFiReconnect() {
    if (WiFi.status() == WL_CONNECTED) return;

    unsigned long now = millis();
    if (now - _lastReconnectAttempt < WIFI_RECONNECT_INTERVAL) return;

    _lastReconnectAttempt = now;
    Serial.println("[WifiHelper] Connexion perdue — tentative de reconnexion...");

    WiFi.disconnect();
    connectToWiFi(WIFI_SSI_Saved, WIFI_PASSWORD_Saved);

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WifiHelper] Reconnexion réussie");
    } else {
        Serial.println("[WiFi] Reconnexion échouée — nouvel essai dans 5 s");
    }
}