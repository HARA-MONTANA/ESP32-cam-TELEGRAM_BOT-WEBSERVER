/*
 * ESP32-CAM Media Server con Bot de Telegram
 *
 * Funcionalidades:
 * - Captura de fotos y streaming de video
 * - Dashboard web para configuracion visual
 * - Bot de Telegram para control remoto
 * - Almacenamiento en tarjeta SD
 * - Foto del dia automatica
 *
 * Configuracion en Arduino IDE:
 *   1. Placa: "AI Thinker ESP32-CAM"
 *   2. Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 *   3. PSRAM: "Enabled"
 *
 * Librerias requeridas (instalar desde Gestor de Librerias):
 *   - ArduinoJson by Benoit Blanchon (v6.x)
 *   - UniversalTelegramBot by Brian Lough
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
#include "sleep_manager.h"
#include "recording_handler.h"

// Variables para control de tiempo
unsigned long lastNTPSync = 0;
int lastDailyPhotoDay = -1;
bool systemReady = false;

// Variables para reconexion WiFi en loop
unsigned long lastWiFiRetry = 0;
int wifiRetryCount = 0;
#define WIFI_MAX_RETRIES_SETUP 5        // Reintentos durante setup inicial
#define WIFI_RETRY_INTERVAL_LOOP 30000  // 30s entre reintentos en loop (base)
#define WIFI_MAX_BACKOFF 4              // Maximo exponente para backoff (30s * 2^4 = ~8 min)
#define WIFI_CONNECT_TIMEOUT 15000      // 15s timeout por intento de conexion

// Variables para monitoreo de salud del sistema
unsigned long lastHealthCheck = 0;
#define HEALTH_CHECK_INTERVAL 60000     // Chequeo de salud cada 60 segundos
#define HEAP_CRITICAL_THRESHOLD 20000   // Reiniciar si heap baja de 20KB

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
    Serial.println("  con Bot de Telegram");
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
        Serial.println("SD Card OK");
        // Reparar AVIs que quedaron sin finalizar por reinicios inesperados
        recordingHandler.repairRecordings();
        Serial.println();
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

    // Inicializar modo sleep
    sleepManager.begin();

    // Sistema listo
    systemReady = true;
    Serial.println("\n================================");
    Serial.println("  Sistema iniciado correctamente");
    Serial.println("================================");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("IP: http://%s\n", WiFi.localIP().toString().c_str());
        Serial.printf("Dashboard: http://%s/\n", WiFi.localIP().toString().c_str());
        Serial.printf("Stream: http://%s/stream\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("WiFi: No conectado (reintentando en segundo plano)");
    }
    Serial.println("================================\n");
}

void loop() {
    if (!systemReady) return;

    // Verificar auto-sleep por inactividad
    sleepManager.checkAutoSleep();

    // Capturar frames de grabacion si hay una grabacion activa
    recordingHandler.update();

    // Manejar servidor web (siempre activo; las conexiones despiertan el sistema)
    webServer.handleClient();

    // Manejar mensajes de Telegram
    // (en modo sleep el checkInterval es mayor → menos polling)
    telegramBot.handleMessages();

    // Verificar si es hora de la foto del dia
    checkDailyPhoto();

    // Reconectar WiFi si se desconecta (con backoff exponencial)
    if (WiFi.status() != WL_CONNECTED) {
        int backoffExponent = min(wifiRetryCount, WIFI_MAX_BACKOFF);
        unsigned long retryInterval = WIFI_RETRY_INTERVAL_LOOP * (1UL << backoffExponent);
        if (millis() - lastWiFiRetry > retryInterval) {
            wifiRetryCount++;
            Serial.printf("WiFi desconectado, reintento #%d (proximo en %lus)...\n",
                          wifiRetryCount, retryInterval / 1000);
            lastWiFiRetry = millis();
            connectWiFi();
        }
    } else if (wifiRetryCount > 0) {
        // WiFi se reconecto, resetear contador y reinicializar bot
        Serial.println("WiFi reconectado exitosamente.");
        wifiRetryCount = 0;
        telegramBot.reinitBot();
    }

    // Sincronizar NTP periodicamente (con validacion)
    if (millis() - lastNTPSync > NTP_SYNC_INTERVAL) {
        configTime(credentialsManager.getGmtOffsetSec(), DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 5000)) {
            lastNTPSync = millis();
            Serial.printf("NTP sincronizado: %02d:%02d:%02d\n",
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        } else {
            // Reintentar en 5 minutos en vez de esperar 1 hora
            lastNTPSync = millis() - NTP_SYNC_INTERVAL + 300000;
            Serial.println("Fallo sincronizacion NTP, reintentando en 5 min");
        }
    }

    // Monitoreo de salud del sistema
    if (millis() - lastHealthCheck > HEALTH_CHECK_INTERVAL) {
        lastHealthCheck = millis();
        uint32_t freeHeap = ESP.getFreeHeap();
        Serial.printf("[Salud] Heap: %u bytes | PSRAM: %u bytes | WiFi: %s (RSSI: %d)\n",
                      freeHeap, ESP.getFreePsram(),
                      WiFi.status() == WL_CONNECTED ? "OK" : "DESCONECTADO",
                      WiFi.RSSI());

        // Si el heap esta criticamente bajo, reiniciar para evitar crashes
        if (freeHeap < HEAP_CRITICAL_THRESHOLD) {
            Serial.println("[Salud] CRITICO: Heap muy bajo, reiniciando ESP32...");
            delay(1000);
            ESP.restart();
        }
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

// Setup inicial: reintenta varias veces con backoff, continua sin WiFi si falla
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

    Serial.println("ADVERTENCIA: No se pudo conectar a WiFi despues de varios intentos.");
    Serial.println("Verifica SSID y contrasena.");
    Serial.println("El sistema continuara sin WiFi e intentara reconectar automaticamente.");
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

    // Usar ventana de 5 minutos para no perder la foto si el loop se bloquea
    int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int targetMinutes = config.hour * 60 + config.minute;

    if (currentMinutes >= targetMinutes &&
        currentMinutes < targetMinutes + 5 &&
        timeinfo.tm_mday != lastDailyPhotoDay) {

        Serial.println("Hora de la foto del dia!");
        lastDailyPhotoDay = timeinfo.tm_mday;

        // Tomar foto del dia
        // Siempre se guarda en SD, pero solo se envia a Telegram si esta habilitado
        telegramBot.takeDailyPhoto(config.enabled);
    }
}
