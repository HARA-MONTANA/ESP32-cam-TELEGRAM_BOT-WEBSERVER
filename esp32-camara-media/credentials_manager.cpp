#include "credentials_manager.h"
#include <Preferences.h>

CredentialsManager credentialsManager;
static Preferences credPrefs;

CredentialsManager::CredentialsManager() : credentialsLoaded(false) {
    // Valores por defecto vacíos
    credentials.wifiSSID = "";
    credentials.wifiPassword = "";
    credentials.botToken = "";
    credentials.gmtOffsetSec = -18000; // UTC-5 por defecto
}

void CredentialsManager::init() {
    // Configurar pin del botón como entrada con pull-up
    pinMode(BYPASS_BUTTON_PIN, INPUT_PULLUP);

    // Cargar credenciales guardadas
    loadCredentials();
}

bool CredentialsManager::isBypassButtonPressed() {
    // El botón está presionado cuando el pin está en LOW
    return digitalRead(BYPASS_BUTTON_PIN) == LOW;
}

bool CredentialsManager::hasStoredCredentials() {
    return credentials.wifiSSID.length() > 0 &&
           credentials.wifiPassword.length() > 0 &&
           credentials.botToken.length() > 0;
}

void CredentialsManager::loadCredentials() {
    credPrefs.begin("credentials", true); // Solo lectura
    credentials.wifiSSID = credPrefs.getString("ssid", "");
    credentials.wifiPassword = credPrefs.getString("password", "");
    credentials.botToken = credPrefs.getString("botToken", "");
    credentials.gmtOffsetSec = credPrefs.getLong("gmtOffset", -18000);
    credPrefs.end();

    credentialsLoaded = true;

    if (hasStoredCredentials()) {
        Serial.println("Credenciales anteriores encontradas en memoria.");
    } else {
        Serial.println("No hay credenciales guardadas previamente.");
    }
}

void CredentialsManager::saveCredentials() {
    credPrefs.begin("credentials", false); // Lectura/Escritura
    credPrefs.putString("ssid", credentials.wifiSSID);
    credPrefs.putString("password", credentials.wifiPassword);
    credPrefs.putString("botToken", credentials.botToken);
    credPrefs.putLong("gmtOffset", credentials.gmtOffsetSec);
    credPrefs.end();

    Serial.println("Credenciales guardadas en memoria.");
}

String CredentialsManager::readSerialLineWithTimeout(unsigned long timeout, bool* buttonPressed) {
    String input = "";
    unsigned long startTime = millis();
    bool hasTimeout = (timeout > 0);

    while (!hasTimeout || (millis() - startTime < timeout)) {
        // Verificar si el botón de bypass fue presionado
        if (buttonPressed && isBypassButtonPressed()) {
            *buttonPressed = true;
            Serial.println("\n[Boton presionado - usando credenciales guardadas]");
            return "";
        }

        if (Serial.available()) {
            char c = Serial.read();

            if (c == '\n' || c == '\r') {
                if (input.length() > 0 || c == '\n') {
                    Serial.println();
                    return input;
                }
                continue;
            }

            input += c;
            Serial.print(c);
        }

        delay(10);
    }

    Serial.println(" (timeout)");
    return input;
}

bool CredentialsManager::requestValue(const char* prompt, String& value, const String& savedValue, bool isPassword, bool* buttonPressed) {
    Serial.println();

    bool hasSaved = (savedValue.length() > 0);
    unsigned long timeout = hasSaved ? CREDENTIAL_TIMEOUT : 0;

    // Mostrar el valor guardado (parcialmente oculto si es password)
    if (hasSaved) {
        if (isPassword) {
            Serial.printf("%s [****guardado****] (timeout: %ds): ", prompt, CREDENTIAL_TIMEOUT / 1000);
        } else {
            Serial.printf("%s [%s] (timeout: %ds): ", prompt, savedValue.c_str(), CREDENTIAL_TIMEOUT / 1000);
        }
    } else {
        Serial.printf("%s (sin valor guardado, esperando entrada): ", prompt);
    }

    String input = readSerialLineWithTimeout(timeout, buttonPressed);

    // Si se presionó el botón, salir inmediatamente
    if (buttonPressed && *buttonPressed) {
        value = savedValue;
        return false;
    }

    // Si el usuario presionó Enter sin escribir nada, usar valor guardado
    if (input.length() == 0) {
        if (hasSaved) {
            value = savedValue;
            if (isPassword) {
                Serial.printf("  >> %s = ****guardado****\n", prompt);
            } else {
                Serial.printf("  >> %s = %s (guardado)\n", prompt, savedValue.c_str());
            }
            return false;
        } else {
            value = "";
            Serial.printf("  >> %s = (vacio)\n", prompt);
            return false;
        }
    }

    // Usuario ingresó un nuevo valor
    value = input;
    if (isPassword) {
        Serial.printf("  >> %s = ****nuevo****\n", prompt);
    } else {
        Serial.printf("  >> %s = %s (nuevo)\n", prompt, input.c_str());
    }
    return true;
}

bool CredentialsManager::requestTimezone(long& offset, long savedOffset, bool* buttonPressed) {
    Serial.println();

    // Mostrar opciones comunes de timezone
    Serial.println("Zonas horarias comunes:");
    Serial.println("  UTC-5: Colombia, Peru, Ecuador, Panama");
    Serial.println("  UTC-6: Mexico Centro, Costa Rica");
    Serial.println("  UTC-4: Venezuela, Bolivia, Puerto Rico");
    Serial.println("  UTC-3: Argentina, Chile, Brasil (Este)");
    Serial.println("  UTC+0: UK, Portugal");
    Serial.println("  UTC+1: Espana, Francia, Alemania");

    int savedHours = savedOffset / 3600;
    Serial.printf("\nIngrese offset UTC en horas (ej: -5, +1) [%+d] (timeout: %ds): ", savedHours, CREDENTIAL_TIMEOUT / 1000);

    String input = readSerialLineWithTimeout(CREDENTIAL_TIMEOUT, buttonPressed);

    // Si se presionó el botón, salir inmediatamente
    if (buttonPressed && *buttonPressed) {
        offset = savedOffset;
        return false;
    }

    if (input.length() == 0) {
        offset = savedOffset;
        Serial.printf("  >> Timezone = UTC%+d (guardado)\n", savedHours);
        return false;
    }

    // Parsear el valor ingresado
    int hours = input.toInt();

    // Validar rango (-12 a +14)
    if (hours < -12 || hours > 14) {
        Serial.printf("  Valor invalido, usando guardado: UTC%+d\n", savedHours);
        offset = savedOffset;
        return false;
    }

    offset = hours * 3600L;
    Serial.printf("  >> Timezone = UTC%+d (nuevo)\n", hours);
    return true;
}

bool CredentialsManager::requestCredentials() {
    Serial.println("\n========================================");
    Serial.println("  CONFIGURACION DE CREDENCIALES");
    Serial.println("========================================");

    // Verificar si el botón de bypass está presionado al inicio
    if (isBypassButtonPressed() && hasStoredCredentials()) {
        Serial.println("\nBoton de bypass detectado (PIN 15 = LOW)");
        Serial.println("Usando credenciales guardadas...");
        Serial.printf("  WiFi SSID: %s\n", credentials.wifiSSID.c_str());
        Serial.printf("  Bot Token: %s...%s\n",
                      credentials.botToken.substring(0, 10).c_str(),
                      credentials.botToken.substring(credentials.botToken.length() - 5).c_str());
        Serial.printf("  Timezone: UTC%+ld\n", credentials.gmtOffsetSec / 3600);
        Serial.println("========================================\n");
        return true;
    }

    if (!hasStoredCredentials()) {
        Serial.println("\nNo hay credenciales guardadas.");
        Serial.println("Debe ingresar las credenciales.");
    }

    Serial.println("\nIngrese las credenciales por serial.");
    Serial.println("- Con valor guardado: timeout de " + String(CREDENTIAL_TIMEOUT / 1000) + "s, ENTER o timeout usa el guardado");
    Serial.println("- Sin valor guardado: espera hasta que ingrese un valor");
    Serial.println("- Presione el BOTON (GPIO15) para saltar y usar guardadas");
    Serial.println("----------------------------------------");

    bool anyChanged = false;
    bool buttonPressed = false;

    // Solicitar SSID WiFi
    {
        String tempSSID;
        if (requestValue("WiFi SSID", tempSSID, credentials.wifiSSID, false, &buttonPressed)) {
            credentials.wifiSSID = tempSSID;
            anyChanged = true;
        } else if (tempSSID.length() > 0) {
            credentials.wifiSSID = tempSSID;
        }
    }

    // Solicitar Password WiFi
    if (!(buttonPressed && hasStoredCredentials())) {
        String tempPassword;
        if (requestValue("WiFi Password", tempPassword, credentials.wifiPassword, true, &buttonPressed)) {
            credentials.wifiPassword = tempPassword;
            anyChanged = true;
        } else if (tempPassword.length() > 0) {
            credentials.wifiPassword = tempPassword;
        }
    }

    // Solicitar Bot Token
    if (!(buttonPressed && hasStoredCredentials())) {
        String tempToken;
        if (requestValue("Bot Token de Telegram", tempToken, credentials.botToken, false, &buttonPressed)) {
            credentials.botToken = tempToken;
            anyChanged = true;
        } else if (tempToken.length() > 0) {
            credentials.botToken = tempToken;
        }
    }

    // Solicitar Timezone
    if (!(buttonPressed && hasStoredCredentials())) {
        long tempOffset;
        if (requestTimezone(tempOffset, credentials.gmtOffsetSec, &buttonPressed)) {
            credentials.gmtOffsetSec = tempOffset;
            anyChanged = true;
        } else {
            credentials.gmtOffsetSec = tempOffset;
        }
    }

    // Guardar si hubo cambios
    if (anyChanged) {
        saveCredentials();
    }

    // Mostrar resumen completo de todos los datos
    Serial.println("\n========================================");
    Serial.println("  RESUMEN DE CREDENCIALES CONFIGURADAS");
    Serial.println("========================================");
    Serial.printf("  WiFi SSID:    %s\n", credentials.wifiSSID.length() > 0 ? credentials.wifiSSID.c_str() : "(vacio)");
    Serial.printf("  WiFi Pass:    %s\n", credentials.wifiPassword.length() > 0 ? "********" : "(vacio)");
    Serial.printf("  Bot Token:    %s\n", credentials.botToken.length() > 10 ?
                  (credentials.botToken.substring(0, 10) + "..." + credentials.botToken.substring(credentials.botToken.length() - 5)).c_str() :
                  (credentials.botToken.length() > 0 ? credentials.botToken.c_str() : "(no configurado)"));
    Serial.printf("  Timezone:     UTC%+ld\n", credentials.gmtOffsetSec / 3600);
    Serial.println("========================================\n");

    return hasStoredCredentials();
}

String CredentialsManager::getWifiSSID() {
    return credentials.wifiSSID;
}

String CredentialsManager::getWifiPassword() {
    return credentials.wifiPassword;
}

String CredentialsManager::getBotToken() {
    return credentials.botToken;
}

long CredentialsManager::getGmtOffsetSec() {
    return credentials.gmtOffsetSec;
}
