/*
 * ESP32-CAM Media Server con Bot de Telegram y Discord
 *
 * Funcionalidades:
 * - Captura de fotos y streaming de video
 * - Dashboard web para configuracion visual
 * - Bot de Telegram para control remoto
 * - Bot de Discord (requiere PlatformIO con -DDISCORD_ENABLED)
 * - Almacenamiento en tarjeta SD
 * - Foto del dia automatica
 *
 * Para Arduino IDE:
 *   1. Configura la placa: "AI Thinker ESP32-CAM"
 *   2. Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 *   3. PSRAM: "Enabled"
 *   Nota: Discord no esta disponible en Arduino IDE (requiere PlatformIO).
 *
 * Librerias requeridas (instalar desde Gestor de Librerias):
 *   - ArduinoJson by Benoit Blanchon (v6.x)
 *   - UniversalTelegramBot by Brian Lough
 *
 * Para PlatformIO:
 *   Usa platformio.ini (este mismo archivo es el entry point)
 *
 * Hardware: ESP32-CAM AI-Thinker con OV2640
 */

#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "credentials_manager.h"
#include "camera_handler.h"
#include "web_server.h"
#include "telegram_bot.h"
#include "sd_handler.h"

#ifdef DISCORD_ENABLED
#include "discord_bot.h"
#endif

// Variables para control de tiempo
unsigned long lastNTPSync = 0;
int lastDailyPhotoDay = -1;
bool systemReady = false;

// Variables para reconexion WiFi en loop
unsigned long lastWiFiRetry = 0;
int wifiRetryCount = 0;
#define WIFI_MAX_RETRIES_SETUP 5        // Reintentos durante setup inicial
#define WIFI_RETRY_INTERVAL_LOOP 30000  // 30s entre reintentos en loop
#define WIFI_CONNECT_TIMEOUT 15000      // 15s timeout por intento de conexion

// Declaracion de funciones
bool connectWiFi();
void setupWiFi();
void setupTime();
void checkDailyPhoto();

void setup() {
    // Inicializar Serial
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    delay(1000);

    Serial.println("\n\n================================");
    Serial.println("  ESP32-CAM Media Server");
    #ifdef DISCORD_ENABLED
    Serial.println("  con Telegram y Discord Bot");
    #else
    Serial.println("  con Bot de Telegram");
    #endif
    Serial.println("================================\n");

    // Inicializar gestor de credenciales
    credentialsManager.init();

    // Solicitar credenciales (o usar guardadas si boton presionado)
    if (!credentialsManager.requestCredentials()) {
        Serial.println("ERROR: No hay credenciales configuradas");
        Serial.println("Reiniciando en 5 segundos...");
        delay(5000);
        ESP.restart();
    }

    // Inicializar camara
    Serial.println("[1/5] Inicializando camara...");
    if (!camera.init()) {
        Serial.println("ERROR: No se pudo inicializar la camara");
        Serial.println("Reiniciando en 5 segundos...");
        delay(5000);
        ESP.restart();
    }
    Serial.println("Camara OK\n");

    // Inicializar SD Card
    Serial.println("[2/5] Inicializando tarjeta SD...");
    if (!sdCard.init()) {
        Serial.println("ADVERTENCIA: SD no disponible, continuando sin almacenamiento local");
    } else {
        Serial.println("SD Card OK\n");
    }

    // Conectar WiFi
    Serial.println("[3/5] Conectando a WiFi...");
    setupWiFi();

    // Configurar hora (NTP)
    Serial.println("[4/5] Sincronizando hora...");
    setupTime();

    // Inicializar servidor web
    Serial.println("[5/5] Iniciando servicios...");
    webServer.init();

    // Inicializar bot de Telegram
    telegramBot.init();

    // Inicializar bot de Discord (solo PlatformIO con -DDISCORD_ENABLED)
    #ifdef DISCORD_ENABLED
    discordBot.init();
    #endif

    // Sistema listo
    systemReady = true;
    Serial.println("\n================================");
    Serial.println("  Sistema iniciado correctamente");
    Serial.println("================================");
    Serial.printf("IP: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Dashboard: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("Stream: http://%s/stream\n", WiFi.localIP().toString().c_str());
    Serial.println("================================\n");
}

void loop() {
    if (!systemReady) return;

    // Manejar servidor web
    webServer.handleClient();

    // Manejar mensajes de Telegram
    telegramBot.handleMessages();

    // Manejar mensajes de Discord
    #ifdef DISCORD_ENABLED
    discordBot.handleMessages();
    #endif

    // Verificar si es hora de la foto del dia
    checkDailyPhoto();

    // Reconectar WiFi si se desconecta (sin reiniciar el sistema)
    if (WiFi.status() != WL_CONNECTED) {
        if (millis() - lastWiFiRetry > WIFI_RETRY_INTERVAL_LOOP) {
            wifiRetryCount++;
            Serial.printf("WiFi desconectado, reintento #%d...\n", wifiRetryCount);
            lastWiFiRetry = millis();
            connectWiFi();
        }
    } else if (wifiRetryCount > 0) {
        // WiFi se reconecto, resetear contador
        Serial.println("WiFi reconectado exitosamente.");
        wifiRetryCount = 0;
    }

    // Sincronizar NTP periodicamente
    if (millis() - lastNTPSync > NTP_SYNC_INTERVAL) {
        configTime(credentialsManager.getGmtOffsetSec(), DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        lastNTPSync = millis();
        Serial.println("Hora sincronizada con NTP");
    }
}

// Intenta conectar a WiFi una vez. Retorna true si conecta, false si no.
bool connectWiFi() {
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(credentialsManager.getWifiSSID().c_str(),
               credentialsManager.getWifiPassword().c_str());

    Serial.print("Conectando a ");
    Serial.print(credentialsManager.getWifiSSID());

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime < WIFI_CONNECT_TIMEOUT)) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi conectado!");
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("RSSI: %d dBm\n\n", WiFi.RSSI());
        wifiRetryCount = 0;
        return true;
    }

    Serial.println("\nNo se pudo conectar a WiFi.");
    return false;
}

// Setup inicial: reintenta varias veces con backoff antes de reiniciar
void setupWiFi() {
    for (int i = 0; i < WIFI_MAX_RETRIES_SETUP; i++) {
        if (i > 0) {
            int waitSec = (1 << i) * 2; // backoff: 4s, 8s, 16s, 32s
            Serial.printf("Reintento %d/%d en %ds...\n", i + 1, WIFI_MAX_RETRIES_SETUP, waitSec);
            delay(waitSec * 1000);
        }

        if (connectWiFi()) {
            return;
        }
    }

    Serial.println("Error: No se pudo conectar despues de varios intentos.");
    Serial.println("Verifica SSID y contrasena.");
    Serial.println("Reiniciando en 10 segundos...");
    delay(10000);
    ESP.restart();
}

void setupTime() {
    configTime(credentialsManager.getGmtOffsetSec(), DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    Serial.print("Esperando sincronizacion NTP");
    struct tm timeinfo;
    int attempts = 0;

    while (!getLocalTime(&timeinfo) && attempts < 10) {
        Serial.print(".");
        delay(1000);
        attempts++;
    }

    if (getLocalTime(&timeinfo)) {
        Serial.println(" OK");
        Serial.printf("Hora actual: %02d/%02d/%04d %02d:%02d:%02d\n\n",
                      timeinfo.tm_mday,
                      timeinfo.tm_mon + 1,
                      timeinfo.tm_year + 1900,
                      timeinfo.tm_hour,
                      timeinfo.tm_min,
                      timeinfo.tm_sec);

        lastDailyPhotoDay = timeinfo.tm_mday;
        lastNTPSync = millis();
    } else {
        Serial.println(" FALLO");
        Serial.println("Continuando sin hora sincronizada\n");
    }
}

void checkDailyPhoto() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return;
    }

    // Obtener configuracion actual de foto diaria de Telegram
    DailyPhotoConfig config = telegramBot.getDailyPhotoConfig();

    // Verificar si es la hora configurada para la foto del dia
    if (timeinfo.tm_hour == config.hour &&
        timeinfo.tm_min == config.minute &&
        timeinfo.tm_mday != lastDailyPhotoDay) {

        Serial.println("Hora de la foto del dia!");
        lastDailyPhotoDay = timeinfo.tm_mday;

        // Tomar foto del dia
        // Siempre se guarda en SD, pero solo se envia a Telegram si esta habilitado
        telegramBot.takeDailyPhoto(config.enabled);

        #ifdef DISCORD_ENABLED
        // Enviar a Discord si esta habilitado (configuracion independiente)
        DiscordDailyConfig discordConfig = discordBot.getDailyPhotoConfig();
        if (discordConfig.enabled) {
            // Solo enviar, no volver a capturar (ya se guardo en SD)
            discordBot.sendSavedDailyPhoto();
        }
        #endif
    }
}
