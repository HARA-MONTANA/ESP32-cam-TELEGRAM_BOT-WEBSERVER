/*
 * Discord Bot para ESP32-CAM
 *
 * Requiere PlatformIO con framework dual (arduino + espidf)
 * y la libreria esp-discord (https://github.com/abobija/esp-discord)
 *
 * Compilar con: pio run -e esp32cam_discord
 * NO compatible con Arduino IDE.
 */

#ifndef DISCORD_BOT_H
#define DISCORD_BOT_H

#ifdef DISCORD_ENABLED

#include <Arduino.h>

// Colores para embeds (Discord usa formato decimal)
// Algunos colores ya definidos en esp-discord/include/discord.h, evitar redefinición
#ifndef DISCORD_COLOR_PURPLE
#define DISCORD_COLOR_PURPLE    0x9B59B6  // Morado vistoso
#endif
#ifndef DISCORD_COLOR_BLURPLE
#define DISCORD_COLOR_BLURPLE   0x5865F2  // Morado Discord oficial
#endif
#ifndef DISCORD_COLOR_SUCCESS
#define DISCORD_COLOR_SUCCESS   0x57F287  // Verde éxito
#endif
#ifndef DISCORD_COLOR_ERROR
#define DISCORD_COLOR_ERROR     0xED4245  // Rojo error
#endif
#ifndef DISCORD_COLOR_WARNING
#define DISCORD_COLOR_WARNING   0xFEE75C  // Amarillo advertencia
#endif

// Estructura para configuración de foto diaria (compartida con Telegram)
struct DiscordDailyConfig {
    int hour;
    int minute;
    bool useFlash;
    bool enabled;  // Envío automático a Discord
};

class DiscordBot {
public:
    DiscordBot();

    // ============================================
    // INICIALIZACIÓN Y LOOP
    // ============================================
    void init();
    void handleMessages();

    // ============================================
    // ENVÍO DE MENSAJES
    // ============================================
    bool sendMessage(const String& message);
    bool sendMessageToChannel(const String& channelId, const String& message);
    bool sendPhoto(const uint8_t* imageData, size_t imageSize, const String& caption = "");
    bool sendPhotoToChannel(const String& channelId, const uint8_t* imageData, size_t imageSize, const String& caption = "");
    bool sendEmbed(const String& title, const String& description, uint32_t color = 0x00FF00);
    bool sendEmbedToChannel(const String& channelId, const String& title, const String& description, uint32_t color = DISCORD_COLOR_PURPLE);

    // ============================================
    // FOTO DEL DÍA
    // ============================================
    bool takeDailyPhoto(bool sendToDiscord);
    bool sendSavedDailyPhoto();
    DiscordDailyConfig getDailyPhotoConfig();
    void setDailyPhotoTime(int hour, int minute);
    void setDailyPhotoFlash(bool useFlash);
    void setDailyPhotoEnabled(bool enabled);
    void saveDailyPhotoConfig();
    void loadDailyPhotoConfig();

    // ============================================
    // ESTADO
    // ============================================
    bool isConnected();
    String getBotUsername();
    void setConnected(bool state);
    void setBotInfo(const String& username, const String& id);

    // ============================================
    // PROCESAMIENTO DE COMANDOS (público para callback externo)
    // ============================================
    void processCommand(const String& command, const String& channelId, const String& userId, const String& username);

private:
    // Estado de conexión
    bool connected;
    String botUsername;
    String botId;
    String lastChannelId;  // Último canal usado (para foto diaria automática)

    // Configuración de foto diaria
    DiscordDailyConfig dailyConfig;

    // Manejo interno de mensajes
    void sendHelpMessage(const String& channelId);
    void sendStatusMessage(const String& channelId);
    void sendDailyConfigMessage(const String& channelId);
};

extern DiscordBot discordBot;

#endif // DISCORD_ENABLED
#endif // DISCORD_BOT_H
