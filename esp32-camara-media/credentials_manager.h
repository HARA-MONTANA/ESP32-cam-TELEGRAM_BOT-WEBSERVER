#ifndef CREDENTIALS_MANAGER_H
#define CREDENTIALS_MANAGER_H

#include <Arduino.h>

// ============================================
// CONFIGURACIÓN DEL BOTÓN DE BYPASS
// ============================================
#define BYPASS_BUTTON_PIN 13     // Pin del botón (LOW = saltar configuración)
                                 // GPIO13: libre en modo SD_MMC 1-bit, seguro para boot
#define CREDENTIAL_TIMEOUT 30000 // Timeout por credencial en ms (30 segundos)

// ============================================
// SOPORTE MULTI-RED WiFi
// ============================================
#define MAX_WIFI_NETWORKS 5

struct WiFiEntry {
    String ssid;
    String password;
};

// Estructura para almacenar las credenciales base
struct Credentials {
    String wifiSSID;
    String wifiPassword;
    String botToken;          // Token de Telegram
    long gmtOffsetSec;
};

class CredentialsManager {
public:
    CredentialsManager();

    // Inicializar el gestor de credenciales
    void init();

    // Solicitar credenciales (retorna true si se completó, false si usó guardadas)
    bool requestCredentials();

    // Getters base (delegan a la red activa)
    String getWifiSSID();
    String getWifiPassword();
    String getBotToken();
    long getGmtOffsetSec();

    // Verificar si el botón de bypass está presionado (con debounce)
    bool isBypassButtonPressed();

    // No-op: GPIO13 no forma parte del bus SD_MMC en modo 1-bit, por lo que
    // INPUT_PULLUP puede permanecer activo sin causar parpadeo. Se conserva
    // por compatibilidad de interfaz.
    void releaseBypassPin();

    // Verificar si hay credenciales guardadas
    bool hasStoredCredentials();

    // ── Multi-WiFi ────────────────────────────────────────────────
    int       getNetworkCount();
    int       getActiveNetworkIndex();
    WiFiEntry getNetwork(int index);
    bool      addNetwork(const String& ssid, const String& password);
    bool      updateNetwork(int index, const String& ssid, const String& password);
    bool      deleteNetwork(int index);
    void      setActiveNetworkIndex(int index);

private:
    Credentials credentials;
    bool credentialsLoaded;

    // Almacenamiento multi-red
    WiFiEntry wifiNetworks[MAX_WIFI_NETWORKS];
    int wifiNetworkCount;
    int activeNetworkIndex;

    // Cargar credenciales desde Preferences
    void loadCredentials();

    // Guardar credenciales en Preferences
    void saveCredentials();

    // Multi-WiFi: carga, guarda y migración desde clave única legacy
    void loadWiFiNetworks();
    void saveWiFiNetworks();
    void migrateFromSingleNetwork();

    // Solicitar un valor individual por serial con timeout
    // Retorna true si se ingresó un valor nuevo, false si se usó el guardado
    bool requestValue(const char* prompt, String& value, const String& savedValue, bool isPassword = false, bool* buttonPressed = nullptr);

    // Solicitar timezone con validación
    bool requestTimezone(long& offset, long savedOffset, bool* buttonPressed = nullptr);

    // Leer línea del serial con timeout (buttonPressed se pone a true si se presiona GPIO13)
    String readSerialLineWithTimeout(unsigned long timeout, bool* buttonPressed = nullptr);
};

extern CredentialsManager credentialsManager;

#endif // CREDENTIALS_MANAGER_H
