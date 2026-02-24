#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// CREDENCIALES (se solicitan por serial o se cargan de memoria)
// ============================================
// Las credenciales se gestionan dinámicamente por credentials_manager
// - Se solicitan por serial al iniciar
// - Se pueden saltar presionando el botón en GPIO13 (LOW)
// - Cada campo tiene timeout de 30 segundos
// - Presionar ENTER usa el último valor guardado
//
// Para resetear credenciales: borrar la partición NVS o usar
// esptool.py erase_flash antes de subir el nuevo código

// ============================================
// CONFIGURACIÓN DE LA CÁMARA
// ============================================
// Pines para ESP32-CAM AI-Thinker
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ============================================
// CONFIGURACIÓN DEL SERVIDOR WEB
// ============================================
#define WEB_SERVER_PORT 80

// ============================================
// CONFIGURACIÓN DE FOTO DEL DÍA (valores por defecto)
// ============================================
// Hora para enviar la foto del día (formato 24h)
// Estos valores se pueden cambiar via Telegram con /hora HH:MM
#define DAILY_PHOTO_HOUR 11
#define DAILY_PHOTO_MINUTE 0
#define DAILY_PHOTO_FLASH false    // Flash desactivado por defecto
#define DAILY_PHOTO_ENABLED true   // Foto diaria habilitada por defecto

// ============================================
// CONFIGURACIÓN DE SD CARD
// ============================================
#define SD_MMC_1BIT_MODE true  // Usar modo 1-bit para liberar el flash LED
#define DEFAULT_PHOTOS_FOLDER "fotos_diarias"  // Nombre de carpeta raíz para fotos diarias
#define TELEGRAM_PHOTOS_FOLDER "fotos_telegram"  // Carpeta para fotos tomadas por Telegram
#define WEB_PHOTOS_FOLDER "fotos_web"            // Carpeta para fotos tomadas desde el dashboard web

// ============================================
// LED FLASH
// ============================================
#define FLASH_GPIO_NUM 4

// ============================================
// INTERVALOS DE TIEMPO (en milisegundos)
// ============================================
#define TELEGRAM_CHECK_INTERVAL 1000   // Revisar mensajes cada 1 segundo
#define NTP_SYNC_INTERVAL 3600000      // Sincronizar hora cada hora

// ============================================
// CONFIGURACIÓN MODO SLEEP / AHORRO DE ENERGÍA
// ============================================
// En modo sleep: WiFi modem sleep activado + polling de Telegram reducido
// Ahorro estimado: ~40-50% de consumo energético en reposo
#define SLEEP_INACTIVITY_TIMEOUT_DEFAULT 600000UL  // 10 min de inactividad → sleep
#define SLEEP_TELEGRAM_INTERVAL          10000UL   // Poll Telegram cada 10s en sleep

// ============================================
// NTP CONFIGURACIÓN
// ============================================
#define NTP_SERVER "pool.ntp.org"
// GMT_OFFSET_SEC ahora se gestiona dinámicamente por credentials_manager
#define DAYLIGHT_OFFSET_SEC 0

// ============================================
// CONFIGURACIÓN DE GRABACIÓN DE VIDEO
// ============================================
#define RECORDINGS_FOLDER "grabaciones"         // Carpeta para guardar videos AVI
#define MAX_RECORDING_SECONDS 300               // Duración máxima: 5 minutos
#define MIN_FREE_SD_MB_FOR_RECORDING 50         // Detener grabación si menos de 50MB libres

#endif // CONFIG_H
