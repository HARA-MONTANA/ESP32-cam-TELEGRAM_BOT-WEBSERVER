/*
 * Discord Bot para ESP32-CAM
 *
 * Implementación completa con esp-discord
 * Comandos equivalentes al bot de Telegram
 *
 * Compilar con: pio run -e esp32cam_discord
 */

#ifdef DISCORD_ENABLED

#include "discord_bot.h"
#include "camera_handler.h"
#include "sd_handler.h"
#include "credentials_manager.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>

// Includes de esp-discord (ESP-IDF)
extern "C" {
    #include "discord.h"
    #include "discord/message.h"
    #include "discord/attachment.h"
    #include "discord/session.h"
    #include "helpers/estr.h"
}

// Instancia global
DiscordBot discordBot;

// Preferences para persistencia
static Preferences discordDailyPrefs;

// Handle del cliente Discord
static discord_handle_t discord_client = NULL;

// Referencia a la instancia para el callback
static DiscordBot* botInstance = NULL;

// ============================================
// CALLBACK DE EVENTOS DE DISCORD
// ============================================
static void discord_event_handler(void* handler_arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    if (!botInstance) return;

    discord_event_data_t* data = (discord_event_data_t*)event_data;

    switch (event_id) {
        case DISCORD_EVENT_CONNECTED: {
            Serial.println("[Discord] Bot conectado!");
            botInstance->setConnected(true);
            // Obtener información del bot
            const discord_session_t* session = NULL;
            if (discord_session_get_current(data->client, &session) == ESP_OK && session && session->user) {
                botInstance->setBotInfo(String(session->user->username), String(session->user->id));
                Serial.printf("[Discord] Bot: %s#%s\n", session->user->username, session->user->discriminator);
            }
            break;
        }

        case DISCORD_EVENT_DISCONNECTED: {
            Serial.println("[Discord] Bot desconectado");
            botInstance->setConnected(false);
            break;
        }

        case DISCORD_EVENT_RECONNECTING: {
            Serial.println("[Discord] Reconectando...");
            break;
        }

        case DISCORD_EVENT_MESSAGE_RECEIVED: {
            discord_message_t* msg = (discord_message_t*)data->ptr;

            // Ignorar mensajes del propio bot
            if (msg->author && msg->author->bot) {
                break;
            }

            // Verificar que el mensaje tiene contenido
            if (!msg->content || strlen(msg->content) == 0) {
                break;
            }

            String content = String(msg->content);
            String channelId = msg->channel_id ? String(msg->channel_id) : "";
            String userId = msg->author ? String(msg->author->id) : "";
            String username = msg->author ? String(msg->author->username) : "Unknown";

            Serial.printf("[Discord] Mensaje de %s: %s\n", username.c_str(), content.c_str());

            // Procesar si es un comando (empieza con w!)
            if (content.startsWith("w!")) {
                botInstance->processCommand(content, channelId, userId, username);
            }
            break;
        }

        default:
            break;
    }
}

// ============================================
// CONSTRUCTOR
// ============================================
DiscordBot::DiscordBot()
    : connected(false) {
    // Valores por defecto
    dailyConfig.hour = DAILY_PHOTO_HOUR;
    dailyConfig.minute = DAILY_PHOTO_MINUTE;
    dailyConfig.useFlash = DAILY_PHOTO_FLASH;
    dailyConfig.enabled = false;  // Discord deshabilitado por defecto

    botInstance = this;
}

// ============================================
// INICIALIZACIÓN
// ============================================
void DiscordBot::init() {
    Serial.println("[Discord] Inicializando bot...");

    // Obtener credenciales
    String token = credentialsManager.getDiscordToken();
    lastChannelId = "";  // Se actualiza al recibir el primer comando

    if (token.length() == 0) {
        Serial.println("[Discord] ERROR: No hay token configurado");
        return;
    }

    // Cargar configuración guardada
    loadDailyPhotoConfig();

    // Configurar cliente Discord
    discord_config_t config = {
        .token = (char*)token.c_str(),
        .intents = DISCORD_INTENT_GUILD_MESSAGES | DISCORD_INTENT_MESSAGE_CONTENT
    };

    discord_client = discord_create(&config);
    if (!discord_client) {
        Serial.println("[Discord] ERROR: No se pudo crear el cliente");
        return;
    }

    // Registrar manejador de eventos
    esp_err_t err = discord_register_events(discord_client, DISCORD_EVENT_ANY, discord_event_handler, NULL);
    if (err != ESP_OK) {
        Serial.println("[Discord] ERROR: No se pudo registrar eventos");
        discord_destroy(discord_client);
        discord_client = NULL;
        return;
    }

    // Conectar (no bloqueante)
    err = discord_login(discord_client);
    if (err != ESP_OK) {
        Serial.println("[Discord] ERROR: No se pudo iniciar login");
        discord_destroy(discord_client);
        discord_client = NULL;
        return;
    }

    Serial.println("[Discord] Bot inicializado, conectando...");
}

// ============================================
// LOOP DE MENSAJES
// ============================================
void DiscordBot::handleMessages() {
    // esp-discord maneja los eventos en background
    // No necesitamos hacer polling manual
}

// ============================================
// ENVIAR MENSAJE DE TEXTO
// ============================================
bool DiscordBot::sendMessage(const String& message) {
    return sendMessageToChannel(lastChannelId, message);
}

bool DiscordBot::sendMessageToChannel(const String& channelId, const String& message) {
    if (!discord_client || channelId.length() == 0) {
        return false;
    }

    discord_message_t msg = {
        .content = (char*)message.c_str(),
        .channel_id = (char*)channelId.c_str()
    };

    esp_err_t err = discord_message_send(discord_client, &msg, NULL);

    if (err == ESP_OK) {
        Serial.printf("[Discord] Mensaje enviado a %s\n", channelId.c_str());
        return true;
    } else {
        Serial.printf("[Discord] Error enviando mensaje: %d\n", err);
        return false;
    }
}

// ============================================
// ENVIAR FOTO
// ============================================
bool DiscordBot::sendPhoto(const uint8_t* imageData, size_t imageSize, const String& caption) {
    return sendPhotoToChannel(lastChannelId, imageData, imageSize, caption);
}

bool DiscordBot::sendPhotoToChannel(const String& channelId, const uint8_t* imageData, size_t imageSize, const String& caption) {
    if (!discord_client || channelId.length() == 0 || !imageData || imageSize == 0) {
        return false;
    }

    Serial.printf("[Discord] Enviando foto (%d bytes) a %s\n", imageSize, channelId.c_str());

    // Crear attachment
    discord_attachment_t attachment = {
        .filename = (char*)"photo.jpg",
        .content_type = (char*)"image/jpeg",
        .size = imageSize,
        ._data = (char*)imageData,
        ._data_should_be_freed = false
    };

    // Crear mensaje con el attachment
    discord_message_t msg = {
        .content = caption.length() > 0 ? (char*)caption.c_str() : NULL,
        .channel_id = (char*)channelId.c_str()
    };

    // Agregar attachment al mensaje
    esp_err_t err = discord_message_add_attachment(&msg, &attachment);
    if (err != ESP_OK) {
        Serial.println("[Discord] Error agregando attachment");
        return false;
    }

    // Enviar
    err = discord_message_send(discord_client, &msg, NULL);

    if (err == ESP_OK) {
        Serial.println("[Discord] Foto enviada exitosamente");
        return true;
    } else {
        Serial.printf("[Discord] Error enviando foto: %d\n", err);
        return false;
    }
}

// ============================================
// ENVIAR EMBED
// ============================================
bool DiscordBot::sendEmbed(const String& title, const String& description, uint32_t color) {
    if (!discord_client || lastChannelId.length() == 0) {
        return false;
    }

    // Crear embed usando JSON
    char embedJson[512];
    snprintf(embedJson, sizeof(embedJson),
        "{\"embeds\":[{\"title\":\"%s\",\"description\":\"%s\",\"color\":%u}]}",
        title.c_str(), description.c_str(), color);

    discord_message_t msg = {
        .content = NULL,
        .channel_id = (char*)lastChannelId.c_str()
    };

    // Crear embed
    discord_embed_t embed = {
        .title = (char*)title.c_str(),
        .description = (char*)description.c_str(),
        .color = (int)color
    };

    esp_err_t err = discord_message_add_embed(&msg, &embed);
    if (err != ESP_OK) {
        return false;
    }

    err = discord_message_send(discord_client, &msg, NULL);
    return err == ESP_OK;
}

bool DiscordBot::sendEmbedToChannel(const String& channelId, const String& title, const String& description, uint32_t color) {
    if (!discord_client || channelId.length() == 0) {
        return false;
    }

    discord_message_t msg = {
        .content = NULL,
        .channel_id = (char*)channelId.c_str()
    };

    // Crear embed
    discord_embed_t embed = {
        .title = (char*)title.c_str(),
        .description = (char*)description.c_str(),
        .color = (int)color
    };

    esp_err_t err = discord_message_add_embed(&msg, &embed);
    if (err != ESP_OK) {
        return false;
    }

    err = discord_message_send(discord_client, &msg, NULL);
    return err == ESP_OK;
}

// ============================================
// PROCESAR COMANDOS
// ============================================
void DiscordBot::processCommand(const String& command, const String& channelId, const String& userId, const String& username) {
    // Guardar el canal para usarlo en respuestas automáticas (foto diaria)
    lastChannelId = channelId;

    String cmd = command;
    cmd.toLowerCase();
    cmd.trim();

    // ============================================
    // COMANDOS DE FOTOS
    // ============================================
    if (cmd == "w!foto" || cmd == "w!photo" || cmd == "w!captura") {
        sendEmbedToChannel(channelId,
            "📸 Capturando Foto",
            "Procesando imagen en tiempo real...",
            DISCORD_COLOR_PURPLE);

        camera_fb_t* fb = camera.capturePhoto();
        if (fb) {
            // Obtener timestamp
            struct tm timeinfo;
            String timestamp = "";
            if (getLocalTime(&timeinfo)) {
                char buffer[32];
                strftime(buffer, sizeof(buffer), "%d/%m/%Y • %H:%M:%S", &timeinfo);
                timestamp = String(buffer);
            }

            String caption = "📷 **Foto Capturada**";
            if (timestamp.length() > 0) {
                caption += "\n🕐 " + timestamp;
            }
            sendPhotoToChannel(channelId, fb->buf, fb->len, caption);
            camera.releaseFrame(fb);
        } else {
            sendEmbedToChannel(channelId,
                "❌ Error de Captura",
                "No se pudo obtener la imagen de la cámara.\nIntenta de nuevo.",
                DISCORD_COLOR_ERROR);
        }
    }
    // Foto con fecha: w!foto DD/MM/YYYY
    else if (cmd.startsWith("w!foto ") || cmd.startsWith("w!photo ")) {
        String args = command.substring(command.indexOf(' ') + 1);
        args.trim();

        // Parsear fecha
        int day = 0, month = 0, year = 0;
        args.replace("/", " ");
        args.replace("-", " ");

        int firstSpace = args.indexOf(' ');
        if (firstSpace > 0) {
            day = args.substring(0, firstSpace).toInt();
            String rest = args.substring(firstSpace + 1);
            rest.trim();
            int secondSpace = rest.indexOf(' ');
            if (secondSpace > 0) {
                month = rest.substring(0, secondSpace).toInt();
                year = rest.substring(secondSpace + 1).toInt();
            }
        }

        if (year >= 2020 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
            if (!sdCard.isInitialized()) {
                sendEmbedToChannel(channelId,
                    "💾 SD Card",
                    "La tarjeta SD no está disponible.",
                    DISCORD_COLOR_ERROR);
            } else {
                String photoPath = sdCard.findPhotoByDate(year, month, day);
                if (photoPath.isEmpty()) {
                    char dateStr[16];
                    snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", day, month, year);
                    sendEmbedToChannel(channelId,
                        "🔍 Foto No Encontrada",
                        String("No hay foto guardada del **") + dateStr + "**\n\n📅 Usa `w!galeria` para ver fotos disponibles.",
                        DISCORD_COLOR_WARNING);
                } else {
                    sendEmbedToChannel(channelId,
                        "🔄 Buscando en Archivo",
                        "Recuperando foto de la memoria...",
                        DISCORD_COLOR_PURPLE);
                    size_t photoSize = 0;
                    uint8_t* photoData = sdCard.readPhoto(photoPath, photoSize);
                    if (photoData && photoSize > 0) {
                        char dateStr[16];
                        snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", day, month, year);
                        String caption = "📅 **Foto del Archivo**\n";
                        caption += "🗓️ Fecha: " + String(dateStr);
                        sendPhotoToChannel(channelId, photoData, photoSize, caption);
                        sdCard.freePhotoBuffer(photoData);
                    } else {
                        sendEmbedToChannel(channelId,
                            "❌ Error de Lectura",
                            "No se pudo leer la foto desde la SD.",
                            DISCORD_COLOR_ERROR);
                    }
                }
            }
        } else {
            sendEmbedToChannel(channelId,
                "⚠️ Formato Inválido",
                "**Uso correcto:** `w!foto DD/MM/YYYY`\n\n**Ejemplo:** `w!foto 05/01/2026`\n\n📅 Día: 01-31\n📆 Mes: 01-12\n🗓️ Año: 2020-2099",
                DISCORD_COLOR_WARNING);
        }
    }
    // ============================================
    // COMANDOS DE FOTO DIARIA
    // ============================================
    else if (cmd == "w!fotodiaria") {
        sendEmbedToChannel(channelId,
            "🌅 Foto del Día",
            "Buscando la foto guardada de hoy...",
            DISCORD_COLOR_PURPLE);
        sendSavedDailyPhoto();
    }
    else if (cmd == "w!fotodiaria on") {
        dailyConfig.enabled = true;
        saveDailyPhotoConfig();
        String desc = "✅ El envío automático está **ACTIVADO**\n\n";
        desc += "🕐 **Próxima foto:** " + String(dailyConfig.hour) + ":" +
                (dailyConfig.minute < 10 ? "0" : "") + String(dailyConfig.minute) + "\n";
        desc += "📸 La foto se enviará automáticamente a este canal.";
        sendEmbedToChannel(channelId,
            "🌅 Foto Diaria Activada",
            desc,
            DISCORD_COLOR_SUCCESS);
    }
    else if (cmd == "w!fotodiaria off") {
        dailyConfig.enabled = false;
        saveDailyPhotoConfig();
        String desc = "⏸️ El envío automático está **DESACTIVADO**\n\n";
        desc += "💾 La foto se seguirá guardando en la SD.\n";
        desc += "📷 Usa `w!fotodiaria` para verla manualmente.";
        sendEmbedToChannel(channelId,
            "🌅 Foto Diaria Desactivada",
            desc,
            DISCORD_COLOR_WARNING);
    }
    else if (cmd == "w!config" || cmd == "w!configuracion") {
        sendDailyConfigMessage(channelId);
    }
    // ============================================
    // COMANDOS DE GALERÍA - Lista de fotos con paginación
    // ============================================
    else if (cmd == "w!galeria" || cmd == "w!fotos" || cmd.startsWith("w!galeria ") || cmd.startsWith("w!fotos ")) {
        if (!sdCard.isInitialized()) {
            sendEmbedToChannel(channelId,
                "💾 SD Card",
                "La tarjeta SD no está disponible.",
                DISCORD_COLOR_ERROR);
        } else {
            // Obtener número de página (default = 1)
            int page = 1;
            int spaceIdx = command.indexOf(' ');
            if (spaceIdx > 0) {
                String pageStr = command.substring(spaceIdx + 1);
                pageStr.trim();
                int parsedPage = pageStr.toInt();
                if (parsedPage > 0) page = parsedPage;
            }

            int totalPages = 0;
            String photoList = sdCard.listPhotos(page, 10, &totalPages);

            if (totalPages == 0) {
                sendEmbedToChannel(channelId,
                    "🖼️ Galería de Fotos",
                    "📭 No hay fotos guardadas en la memoria.\n\n📸 Usa `w!foto` para tomar tu primera foto.",
                    DISCORD_COLOR_WARNING);
            } else {
                String desc = photoList;
                desc += "\n━━━━━━━━━━━━━━━━━━━━\n";
                desc += "📄 **Página " + String(page) + " de " + String(totalPages) + "**\n\n";

                // Mostrar navegación
                if (totalPages > 1) {
                    desc += "**Navegación:**\n";
                    if (page > 1) {
                        desc += "◀️ `w!galeria " + String(page - 1) + "`  ";
                    }
                    if (page < totalPages) {
                        desc += "▶️ `w!galeria " + String(page + 1) + "`";
                    }
                    desc += "\n\n";
                }

                desc += "💡 Usa `w!foto DD/MM/YYYY` para ver una foto";

                sendEmbedToChannel(channelId,
                    "🖼️ Galería de Fotos",
                    desc,
                    DISCORD_COLOR_PURPLE);
            }
        }
    }
    // ============================================
    // COMANDOS DE SISTEMA
    // ============================================
    else if (cmd == "w!estado" || cmd == "w!status") {
        sendStatusMessage(channelId);
    }
    // ============================================
    // AYUDA
    // ============================================
    else if (cmd == "w!help") {
        sendHelpMessage(channelId);
    }
    else {
        sendMessageToChannel(channelId, "Comando no reconocido. Usa `w!help`");
    }
}

// ============================================
// MENSAJES DE AYUDA Y ESTADO
// ============================================
void DiscordBot::sendHelpMessage(const String& channelId) {
    String desc = "**📸 FOTOS**\n";
    desc += "`w!foto` - Capturar foto en tiempo real\n";
    desc += "`w!foto DD/MM/YYYY` - Foto de fecha específica\n";
    desc += "`w!galeria` - Ver galería de fotos\n\n";

    desc += "**🌅 FOTO DIARIA**\n";
    desc += "`w!fotodiaria` - Foto del día guardada\n";
    desc += "`w!fotodiaria on/off` - Activar/desactivar envío\n";
    desc += "`w!config` - Ver configuración\n\n";

    desc += "**⚙️ SISTEMA**\n";
    desc += "`w!estado` - Estado del sistema\n";
    desc += "`w!help` - Mostrar esta ayuda";

    sendEmbedToChannel(channelId,
        "📋 Comandos Disponibles",
        desc,
        DISCORD_COLOR_PURPLE);
}

void DiscordBot::sendStatusMessage(const String& channelId) {
    String desc = "**💾 Memoria**\n";
    desc += "🔹 RAM libre: `" + String(ESP.getFreeHeap() / 1024) + " KB`\n";
    desc += "🔹 PSRAM libre: `" + String(ESP.getFreePsram() / 1024) + " KB`\n\n";

    // WiFi
    desc += "**📶 Conexión**\n";
    desc += "🔹 Señal WiFi: `" + String(WiFi.RSSI()) + " dBm`\n";
    desc += "🔹 IP: `" + WiFi.localIP().toString() + "`\n\n";

    // SD Card
    desc += "**💾 Almacenamiento**\n";
    if (sdCard.isInitialized()) {
        desc += "🔹 SD: `" + String(sdCard.getFreeSpace() / (1024 * 1024)) + " MB libres`\n";
        desc += "🔹 Carpeta: `/" + sdCard.getPhotosFolder() + "`\n\n";
    } else {
        desc += "🔹 SD: ❌ No disponible\n\n";
    }

    // Configuración de cámara
    CameraSettings settings = camera.getSettings();
    desc += "**📷 Cámara**\n";
    desc += "🔹 Flash: " + String(settings.flashEnabled ? "✅ ON" : "⭕ OFF") + "\n";
    desc += "🔹 Brillo: `" + String(settings.brightness) + "`\n";
    desc += "🔹 Contraste: `" + String(settings.contrast) + "`\n";
    desc += "🔹 Calidad: `" + String(settings.quality) + "`\n\n";

    // Foto diaria
    desc += "**🌅 Foto Diaria** (" + String(dailyConfig.hour) + ":" +
            (dailyConfig.minute < 10 ? "0" : "") + String(dailyConfig.minute) + ")\n";
    desc += "🔹 Envío Discord: " + String(dailyConfig.enabled ? "✅ ON" : "⭕ OFF") + "\n";
    desc += "🔹 Guardar SD: ✅ SIEMPRE\n";
    desc += "🔹 Flash: " + String(dailyConfig.useFlash ? "✅ ON" : "⭕ OFF");

    sendEmbedToChannel(channelId,
        "⚙️ Estado del Sistema",
        desc,
        DISCORD_COLOR_PURPLE);
}

void DiscordBot::sendDailyConfigMessage(const String& channelId) {
    String desc = "**⚙️ Configuración Actual**\n\n";
    desc += "🕐 **Hora programada:** " + String(dailyConfig.hour) + ":" +
            (dailyConfig.minute < 10 ? "0" : "") + String(dailyConfig.minute) + "\n";
    desc += "📡 **Envío automático:** " + String(dailyConfig.enabled ? "✅ ACTIVADO" : "⏸️ DESACTIVADO") + "\n";
    desc += "💾 **Guardar en SD:** ✅ SIEMPRE\n";

    if (sdCard.isInitialized() && sdCard.photoExistsToday()) {
        desc += "📷 **Foto de hoy:** ✅ GUARDADA\n";
    } else {
        desc += "📷 **Foto de hoy:** ❌ NO DISPONIBLE\n";
    }

    desc += "\n━━━━━━━━━━━━━━━━━━━━\n";
    desc += "**📋 Comandos Disponibles**\n\n";
    desc += "📸 `w!foto` - Tomar foto ahora\n";
    desc += "🌅 `w!fotodiaria` - Ver foto guardada\n";
    desc += "🔄 `w!fotodiaria on/off` - Cambiar envío";

    sendEmbedToChannel(channelId,
        "🌅 Configuración de Foto Diaria",
        desc,
        DISCORD_COLOR_PURPLE);
}

// ============================================
// FOTO DEL DÍA
// ============================================
bool DiscordBot::takeDailyPhoto(bool sendToDiscord) {
    // Guardar estado actual del flash
    CameraSettings currentSettings = camera.getSettings();
    bool previousFlashState = currentSettings.flashEnabled;

    // Configurar flash
    if (dailyConfig.useFlash != previousFlashState) {
        camera.setFlash(dailyConfig.useFlash);
    }

    camera_fb_t* fb = camera.capturePhoto();

    // Restaurar flash
    if (dailyConfig.useFlash != previousFlashState) {
        camera.setFlash(previousFlashState);
    }

    if (!fb) {
        if (sendToDiscord) {
            sendMessage("Error al capturar foto del dia");
        }
        Serial.println("[Discord] Error al capturar foto del dia");
        return false;
    }

    // Guardar en SD
    bool savedToSD = false;
    if (sdCard.isInitialized()) {
        String dailyPath = sdCard.getDailyPhotoPath();
        savedToSD = sdCard.savePhoto(fb->buf, fb->len, dailyPath);
        if (savedToSD) {
            Serial.println("[Discord] Foto del dia guardada: " + dailyPath);
        }
    }

    // Enviar a Discord
    bool sentToDiscord = false;
    if (sendToDiscord) {
        struct tm timeinfo;
        String dateStr = "Foto del dia";
        if (getLocalTime(&timeinfo)) {
            char buffer[32];
            strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M", &timeinfo);
            dateStr = "Foto del dia: " + String(buffer);
            if (dailyConfig.useFlash) {
                dateStr += " (con flash)";
            }
        }
        sentToDiscord = sendPhoto(fb->buf, fb->len, dateStr);
    }

    camera.releaseFrame(fb);
    return savedToSD || sentToDiscord;
}

bool DiscordBot::sendSavedDailyPhoto() {
    if (!sdCard.isInitialized()) {
        sendMessage("SD Card no disponible");
        return false;
    }

    if (!sdCard.photoExistsToday()) {
        sendMessage("No hay foto del dia guardada.\nLa foto se toma automaticamente a las " +
                    String(dailyConfig.hour) + ":" +
                    (dailyConfig.minute < 10 ? "0" : "") + String(dailyConfig.minute));
        return false;
    }

    String dailyPath = sdCard.getDailyPhotoPath();
    size_t photoSize = 0;
    uint8_t* photoData = sdCard.readPhoto(dailyPath, photoSize);

    if (!photoData || photoSize == 0) {
        sendMessage("Error al leer foto del dia desde SD");
        return false;
    }

    struct tm timeinfo;
    String dateStr = "Foto del dia (guardada)";
    if (getLocalTime(&timeinfo)) {
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%d/%m/%Y", &timeinfo);
        dateStr = "Foto del dia: " + String(buffer);
    }

    bool success = sendPhoto(photoData, photoSize, dateStr);
    sdCard.freePhotoBuffer(photoData);
    return success;
}

// ============================================
// CONFIGURACIÓN DE FOTO DIARIA
// ============================================
DiscordDailyConfig DiscordBot::getDailyPhotoConfig() {
    return dailyConfig;
}

void DiscordBot::setDailyPhotoTime(int hour, int minute) {
    dailyConfig.hour = constrain(hour, 0, 23);
    dailyConfig.minute = constrain(minute, 0, 59);
    Serial.printf("[Discord] Hora de foto diaria: %02d:%02d\n", dailyConfig.hour, dailyConfig.minute);
}

void DiscordBot::setDailyPhotoFlash(bool useFlash) {
    dailyConfig.useFlash = useFlash;
}

void DiscordBot::setDailyPhotoEnabled(bool enabled) {
    dailyConfig.enabled = enabled;
}

void DiscordBot::saveDailyPhotoConfig() {
    discordDailyPrefs.begin("discorddaily", false);
    discordDailyPrefs.putInt("hour", dailyConfig.hour);
    discordDailyPrefs.putInt("minute", dailyConfig.minute);
    discordDailyPrefs.putBool("flash", dailyConfig.useFlash);
    discordDailyPrefs.putBool("enabled", dailyConfig.enabled);
    discordDailyPrefs.end();
    Serial.println("[Discord] Configuracion de foto diaria guardada");
}

void DiscordBot::loadDailyPhotoConfig() {
    discordDailyPrefs.begin("discorddaily", true);
    dailyConfig.hour = discordDailyPrefs.getInt("hour", DAILY_PHOTO_HOUR);
    dailyConfig.minute = discordDailyPrefs.getInt("minute", DAILY_PHOTO_MINUTE);
    dailyConfig.useFlash = discordDailyPrefs.getBool("flash", DAILY_PHOTO_FLASH);
    dailyConfig.enabled = discordDailyPrefs.getBool("enabled", false);
    discordDailyPrefs.end();
}

// ============================================
// ESTADO
// ============================================
bool DiscordBot::isConnected() {
    return connected;
}

String DiscordBot::getBotUsername() {
    return botUsername;
}

void DiscordBot::setConnected(bool state) {
    connected = state;
}

void DiscordBot::setBotInfo(const String& username, const String& id) {
    botUsername = username;
    botId = id;
}

#endif // DISCORD_ENABLED
