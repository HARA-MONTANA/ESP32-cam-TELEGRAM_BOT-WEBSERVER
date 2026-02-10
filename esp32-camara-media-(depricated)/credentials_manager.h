#ifndef CREDENTIALS_MANAGER_H
#define CREDENTIALS_MANAGER_H

#include <Arduino.h>

// ============================================
// CONFIGURACIÓN DEL BOTÓN DE BYPASS
// ============================================
#define BYPASS_BUTTON_PIN 15     // Pin del botón (LOW = saltar configuración)
#define CREDENTIAL_TIMEOUT 30000 // Timeout por credencial en ms (30 segundos)

// Estructura para almacenar las credenciales
struct Credentials {
    String wifiSSID;
    String wifiPassword;
    String botToken;          // Token de Telegram
    long gmtOffsetSec;
    #ifdef DISCORD_ENABLED
    String discordToken;      // Token del bot de Discord
    #endif
};

class CredentialsManager {
public:
    CredentialsManager();

    // Inicializar el gestor de credenciales
    void init();

    // Solicitar credenciales (retorna true si se completó, false si usó guardadas)
    bool requestCredentials();

    // Getters para las credenciales
    String getWifiSSID();
    String getWifiPassword();
    String getBotToken();
    long getGmtOffsetSec();

    #ifdef DISCORD_ENABLED
    String getDiscordToken();
    #endif

    // Verificar si el botón de bypass está presionado
    bool isBypassButtonPressed();

    // Verificar si hay credenciales guardadas
    bool hasStoredCredentials();

private:
    Credentials credentials;
    bool credentialsLoaded;

    // Cargar credenciales desde Preferences
    void loadCredentials();

    // Guardar credenciales en Preferences
    void saveCredentials();

    // Solicitar un valor individual por serial con timeout
    // Retorna true si se ingresó un valor nuevo, false si se usó el guardado
    bool requestValue(const char* prompt, String& value, const String& savedValue, bool isPassword = false, bool* buttonPressed = nullptr);

    // Solicitar timezone con validación
    bool requestTimezone(long& offset, long savedOffset, bool* buttonPressed = nullptr);

    // Leer línea del serial con timeout (buttonPressed se pone a true si se presiona GPIO15)
    String readSerialLineWithTimeout(unsigned long timeout, bool* buttonPressed = nullptr);
};

extern CredentialsManager credentialsManager;

#endif // CREDENTIALS_MANAGER_H
