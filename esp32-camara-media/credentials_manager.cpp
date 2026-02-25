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
    wifiNetworkCount = 0;
    activeNetworkIndex = 0;
}

void CredentialsManager::init() {
    // Configurar pin del botón como entrada con pull-up
    pinMode(BYPASS_BUTTON_PIN, INPUT_PULLUP);

    // Cargar credenciales guardadas
    loadCredentials();
}

bool CredentialsManager::isBypassButtonPressed() {
    // Debounce: confirmar 3 lecturas consecutivas en LOW (separadas 20 ms).
    // Evita falsos disparos por transitorios de la línea SD_MMC CMD durante el arranque.
    for (int i = 0; i < 3; i++) {
        if (digitalRead(BYPASS_BUTTON_PIN) != LOW) return false;
        if (i < 2) delay(20);
    }
    return true;
}

void CredentialsManager::releaseBypassPin() {
    // Una vez que las credenciales están cargadas, el botón ya no es necesario.
    // Configurar el pin como OUTPUT LOW para apagar cualquier LED que esté
    // conectado entre GPIO13 y GND (el INPUT_PULLUP previo lo mantendría HIGH).
    pinMode(BYPASS_BUTTON_PIN, OUTPUT);
    digitalWrite(BYPASS_BUTTON_PIN, LOW);
}

bool CredentialsManager::hasStoredCredentials() {
    return wifiNetworkCount > 0 && credentials.botToken.length() > 0;
}

void CredentialsManager::loadCredentials() {
    credPrefs.begin("credentials", true); // Solo lectura
    credentials.wifiSSID = credPrefs.getString("ssid", "");
    credentials.wifiPassword = credPrefs.getString("password", "");
    credentials.botToken = credPrefs.getString("botToken", "");
    credentials.gmtOffsetSec = credPrefs.getLong("gmtOffset", -18000);
    credPrefs.end();

    // Migrar red única legacy a sistema multi-red, luego cargar todas las redes
    migrateFromSingleNetwork();
    loadWiFiNetworks();

    // Sincronizar credentials.wifiSSID con la red 0 para que el flujo serial
    // siga mostrando el SSID guardado correctamente
    if (wifiNetworkCount > 0) {
        credentials.wifiSSID    = wifiNetworks[0].ssid;
        credentials.wifiPassword = wifiNetworks[0].password;
    }

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

    // Reflejar la red del serial setup en el slot 0 del sistema multi-red
    if (credentials.wifiSSID.length() > 0) {
        if (wifiNetworkCount == 0) {
            wifiNetworks[0].ssid     = credentials.wifiSSID;
            wifiNetworks[0].password = credentials.wifiPassword;
            wifiNetworkCount = 1;
        } else {
            wifiNetworks[0].ssid     = credentials.wifiSSID;
            wifiNetworks[0].password = credentials.wifiPassword;
        }
        saveWiFiNetworks();
    }

    Serial.println("Credenciales guardadas en memoria.");
}

// ── Multi-WiFi: migración, carga y guardado ──────────────────────────────────

void CredentialsManager::migrateFromSingleNetwork() {
    credPrefs.begin("credentials", true);
    bool hasLegacySsid = credPrefs.isKey("ssid");
    bool hasMultiKey   = credPrefs.isKey("wf_count");
    String legacySsid  = credPrefs.getString("ssid", "");
    String legacyPass  = credPrefs.getString("password", "");
    credPrefs.end();

    if (hasLegacySsid && !hasMultiKey && legacySsid.length() > 0) {
        Serial.println("Migrando red WiFi legacy al sistema multi-red...");
        credPrefs.begin("credentials", false);
        credPrefs.putString("wf0_ssid", legacySsid);
        credPrefs.putString("wf0_pass", legacyPass);
        credPrefs.putInt("wf_count",  1);
        credPrefs.putInt("wf_active", 0);
        credPrefs.end();
        Serial.printf("  Red migrada: %s\n", legacySsid.c_str());
    }
}

void CredentialsManager::loadWiFiNetworks() {
    credPrefs.begin("credentials", true);
    int count = credPrefs.getInt("wf_count", 0);
    int active = credPrefs.getInt("wf_active", 0);
    credPrefs.end();

    if (count < 0) count = 0;
    if (count > MAX_WIFI_NETWORKS) count = MAX_WIFI_NETWORKS;
    if (active < 0 || active >= count) active = 0;

    wifiNetworkCount  = count;
    activeNetworkIndex = active;

    credPrefs.begin("credentials", true);
    for (int i = 0; i < wifiNetworkCount; i++) {
        String keyS = "wf" + String(i) + "_ssid";
        String keyP = "wf" + String(i) + "_pass";
        wifiNetworks[i].ssid     = credPrefs.getString(keyS.c_str(), "");
        wifiNetworks[i].password = credPrefs.getString(keyP.c_str(), "");
    }
    credPrefs.end();
}

void CredentialsManager::saveWiFiNetworks() {
    credPrefs.begin("credentials", false);
    credPrefs.putInt("wf_count",  wifiNetworkCount);
    credPrefs.putInt("wf_active", activeNetworkIndex);
    for (int i = 0; i < wifiNetworkCount; i++) {
        credPrefs.putString(("wf" + String(i) + "_ssid").c_str(), wifiNetworks[i].ssid);
        credPrefs.putString(("wf" + String(i) + "_pass").c_str(), wifiNetworks[i].password);
    }
    credPrefs.end();
}

// ── Getters y setters multi-red ───────────────────────────────────────────────

int CredentialsManager::getNetworkCount() {
    return wifiNetworkCount;
}

int CredentialsManager::getActiveNetworkIndex() {
    return activeNetworkIndex;
}

WiFiEntry CredentialsManager::getNetwork(int index) {
    if (index < 0 || index >= wifiNetworkCount) {
        WiFiEntry empty;
        return empty;
    }
    return wifiNetworks[index];
}

bool CredentialsManager::addNetwork(const String& ssid, const String& password) {
    if (wifiNetworkCount >= MAX_WIFI_NETWORKS) return false;
    if (ssid.length() == 0) return false;
    wifiNetworks[wifiNetworkCount].ssid     = ssid;
    wifiNetworks[wifiNetworkCount].password = password;
    wifiNetworkCount++;
    saveWiFiNetworks();
    Serial.printf("Red WiFi añadida [%d]: %s\n", wifiNetworkCount - 1, ssid.c_str());
    return true;
}

bool CredentialsManager::updateNetwork(int index, const String& ssid, const String& password) {
    if (index < 0 || index >= wifiNetworkCount) return false;
    if (ssid.length() == 0) return false;
    wifiNetworks[index].ssid     = ssid;
    wifiNetworks[index].password = password;
    saveWiFiNetworks();
    Serial.printf("Red WiFi actualizada [%d]: %s\n", index, ssid.c_str());
    return true;
}

bool CredentialsManager::deleteNetwork(int index) {
    if (index < 0 || index >= wifiNetworkCount) return false;
    // Desplazar redes hacia arriba para llenar el hueco
    for (int i = index; i < wifiNetworkCount - 1; i++) {
        wifiNetworks[i] = wifiNetworks[i + 1];
    }
    wifiNetworks[wifiNetworkCount - 1].ssid     = "";
    wifiNetworks[wifiNetworkCount - 1].password = "";
    wifiNetworkCount--;
    // Ajustar índice activo si quedó fuera de rango
    if (activeNetworkIndex >= wifiNetworkCount && wifiNetworkCount > 0) {
        activeNetworkIndex = wifiNetworkCount - 1;
    } else if (wifiNetworkCount == 0) {
        activeNetworkIndex = 0;
    }
    saveWiFiNetworks();
    Serial.printf("Red WiFi eliminada [%d]. Redes restantes: %d\n", index, wifiNetworkCount);
    return true;
}

void CredentialsManager::setActiveNetworkIndex(int index) {
    if (index < 0 || index >= wifiNetworkCount) return;
    activeNetworkIndex = index;
    saveWiFiNetworks();
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
        Serial.println("\nBoton de bypass detectado (GPIO13 = LOW)");
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
    Serial.println("- Presione el BOTON (GPIO13) para saltar y usar guardadas");
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
    if (wifiNetworkCount > 0 && activeNetworkIndex < wifiNetworkCount) {
        return wifiNetworks[activeNetworkIndex].ssid;
    }
    return credentials.wifiSSID;
}

String CredentialsManager::getWifiPassword() {
    if (wifiNetworkCount > 0 && activeNetworkIndex < wifiNetworkCount) {
        return wifiNetworks[activeNetworkIndex].password;
    }
    return credentials.wifiPassword;
}

String CredentialsManager::getBotToken() {
    return credentials.botToken;
}

long CredentialsManager::getGmtOffsetSec() {
    return credentials.gmtOffsetSec;
}
