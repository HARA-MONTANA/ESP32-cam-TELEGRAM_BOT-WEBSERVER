#include "telegram_bot.h"
#include "camera_handler.h"
#include "sd_handler.h"
#include "config.h"
#include "credentials_manager.h"
#include "sleep_manager.h"
#include <WiFi.h>
#include <Preferences.h>

TelegramBot telegramBot;
static Preferences dailyPrefs;
static Preferences authPrefs;

// Formatea caption de foto con fecha legible desde el nombre del archivo y peso
static String formatPhotoCaption(int photoId, String photoPath, size_t photoSize) {
    int lastSlash = photoPath.lastIndexOf('/');
    String fileName = (lastSlash >= 0) ? photoPath.substring(lastSlash + 1) : photoPath;

    // Manejar prefijo web_
    String datePart = fileName;
    String suffix = "";
    if (datePart.startsWith("web_")) {
        datePart = datePart.substring(4);
        suffix = " (web)";
    }

    String caption;

    // Parsear: YYYY-MM-DD_HH-MM-SS.jpg o YYYY-MM-DD_HH-MM.jpg
    if (datePart.length() >= 16) {
        String year = datePart.substring(0, 4);
        String month = datePart.substring(5, 7);
        String day = datePart.substring(8, 10);
        String hour = datePart.substring(11, 13);
        String minute = datePart.substring(14, 16);
        String second = "";
        if (datePart.length() >= 19 && datePart.charAt(16) == '-') {
            second = ":" + datePart.substring(17, 19);
        }
        caption = "#" + String(photoId) + " - " + day + "/" + month + "/" + year + " " + hour + ":" + minute + second + suffix;
    } else {
        caption = "#" + String(photoId) + " - " + fileName;
    }

    // Agregar peso de la foto
    if (photoSize >= 1024) {
        caption += "\n⚖️ Peso: " + String(photoSize / 1024.0, 1) + " KB";
    } else {
        caption += "\n⚖️ Peso: " + String(photoSize) + " bytes";
    }

    return caption;
}

TelegramBot::TelegramBot()
    : bot(nullptr), lastCheckTime(0), checkInterval(TELEGRAM_CHECK_INTERVAL), authorizedCount(0),
      tempAuthMode(false), tempAuthExpiry(0) {
    // Valores por defecto
    dailyConfig.hour = DAILY_PHOTO_HOUR;
    dailyConfig.minute = DAILY_PHOTO_MINUTE;
    dailyConfig.useFlash = DAILY_PHOTO_FLASH;
    dailyConfig.enabled = DAILY_PHOTO_ENABLED;

    // Inicializar array de IDs y flags de admin
    for (int i = 0; i < MAX_AUTHORIZED_IDS; i++) {
        authorizedIds[i] = "";
        adminFlags[i] = false;
    }
}

void TelegramBot::init() {
    client.setInsecure();  // No verificar certificado SSL
    client.setTimeout(10); // 10 segundos timeout para llamadas API de Telegram

    bot = new UniversalTelegramBot(credentialsManager.getBotToken(), client);
    bot->longPoll = 0;

    // Cargar configuración guardada
    loadDailyPhotoConfig();
    loadAuthorizedIds();

    Serial.println("Bot de Telegram inicializado");
    Serial.printf("Foto diaria: %s - %02d:%02d (Flash: %s)\n",
                  dailyConfig.enabled ? "ACTIVA" : "INACTIVA",
                  dailyConfig.hour, dailyConfig.minute,
                  dailyConfig.useFlash ? "SI" : "NO");

    if (authorizedCount == 0) {
        Serial.println("No hay usuarios autorizados. El primero en escribir sera admin.");
    } else {
        Serial.printf("Usuarios autorizados: %d (Admin: %s)\n", authorizedCount, authorizedIds[0].c_str());
    }

    // Solo enviar mensaje de inicio si hay usuarios autorizados
    if (authorizedCount > 0) {
        String initMsg = "📷 ESP32-CAM iniciada!\n\n";
        initMsg += "📅 Foto diaria: " + String(dailyConfig.enabled ? "✅ ACTIVA" : "⛔ INACTIVA") + "\n";
        if (dailyConfig.enabled) {
            initMsg += "🕐 Hora: " + String(dailyConfig.hour) + ":" +
                       (dailyConfig.minute < 10 ? "0" : "") + String(dailyConfig.minute);
            initMsg += " (⚡ Flash: " + String(dailyConfig.useFlash ? "ON" : "OFF") + ")\n";
        }
        initMsg += "\nUsa /start o /ayuda para ver comandos";
        sendMessage(initMsg);
    }
}

void TelegramBot::reinitBot() {
    // Cerrar conexion SSL anterior y reconfigurar
    client.stop();
    client.setInsecure();
    client.setTimeout(10);
    Serial.println("Bot de Telegram reinicializado tras reconexion WiFi");
}

void TelegramBot::handleMessages() {
    // No intentar si WiFi no esta conectado
    if (WiFi.status() != WL_CONNECTED) return;

    // Verificar expiración del modo de autorización temporal
    if (tempAuthMode && tempAuthExpiry > 0 && millis() >= tempAuthExpiry) {
        tempAuthMode = false;
        tempAuthExpiry = 0;
        sendMessage("⏰ Modo de autorización temporal expirado. Ya no se autorizan nuevos usuarios.");
    }

    if (millis() - lastCheckTime > checkInterval) {
        int numNewMessages = bot->getUpdates(bot->last_message_received + 1);

        // Limitar a 3 lotes para no bloquear el loop principal demasiado tiempo
        int batches = 0;
        while (numNewMessages && batches < 3) {
            for (int i = 0; i < numNewMessages; i++) {
                processMessage(bot->messages[i]);
            }
            numNewMessages = bot->getUpdates(bot->last_message_received + 1);
            batches++;
        }

        lastCheckTime = millis();
    }
}

void TelegramBot::processMessage(telegramMessage& msg) {
    String chatId = String(msg.chat_id);
    String text = msg.text;
    String fromUser = msg.from_name;

    Serial.printf("Mensaje de %s (ID: %s): %s\n", fromUser.c_str(), chatId.c_str(), text.c_str());

    // Si no hay usuarios autorizados, el primero que escriba se convierte en admin
    if (authorizedCount == 0) {
        addAuthorizedId(chatId);
        Serial.println("Primer usuario autorizado como ADMIN: " + chatId);
        String welcomeMsg = "👑 Bienvenido! Eres el administrador.\n\n";
        welcomeMsg += "🆔 Tu ID: " + chatId + "\n\n";
        welcomeMsg += "👥 Comandos de usuarios:\n";
        welcomeMsg += "/users - Ver lista\n";
        welcomeMsg += "/add ID - Agregar\n";
        welcomeMsg += "/remove ID - Eliminar\n\n";
        welcomeMsg += "Usa /ayuda para ver todos los comandos.";
        bot->sendMessage(chatId, welcomeMsg, "");
        return;
    }

    // Modo de autorización temporal: cualquier nuevo usuario queda autorizado automáticamente
    if (tempAuthMode && !isAuthorized(chatId)) {
        if (addAuthorizedId(chatId)) {
            Serial.println("Usuario autorizado en modo temporal: " + chatId + " (" + fromUser + ")");
            String welcomeMsg = "✅ Acceso autorizado automáticamente (modo temporal activo).\n\n";
            welcomeMsg += "🆔 Tu ID: " + chatId + "\n";
            welcomeMsg += "Usa /ayuda para ver los comandos disponibles.";
            bot->sendMessage(chatId, welcomeMsg, "");
            // Notificar al primer admin
            for (int i = 0; i < authorizedCount; i++) {
                if (adminFlags[i] && authorizedIds[i] != chatId) {
                    bot->sendMessage(authorizedIds[i],
                        "👤 Nuevo usuario autorizado en modo temporal:\n" + fromUser + "\n🆔 ID: " + chatId +
                        "\nTotal: " + String(authorizedCount) + " usuarios", "");
                    break;
                }
            }
        } else {
            // Lista llena — no se puede autorizar
            bot->sendMessage(chatId, "⚠️ Limite de usuarios alcanzado. Contacta al administrador.", "");
            return;
        }
    }

    // Verificar que el usuario está autorizado
    if (!isAuthorized(chatId)) {
        bot->sendMessage(chatId, "🔒 No tienes permiso para usar este bot.\nContacta al administrador.", "");
        Serial.println("Intento de acceso no autorizado desde: " + chatId + " (" + fromUser + ")");
        return;
    }

    // Cualquier mensaje autorizado reinicia el temporizador de inactividad
    sleepManager.registerActivity();

    // Procesar comandos
    if (text.startsWith("/")) {
        handleCommand(text, chatId);
    } else {
        bot->sendMessage(chatId, "ℹ️ Usa /ayuda para ver los comandos disponibles.", "");
    }
}

void TelegramBot::handleCommand(String command, String chatId) {
    // Guardar comando original para parsear argumentos
    String originalCommand = command;
    command.toLowerCase();
    command.trim();

    if (command == "/start" || command == "/ayuda" || command == "/help") {
        sendHelpMessage(chatId);
    }
    else if ((command.startsWith("/foto") && !command.startsWith("/fotodiaria")) || command.startsWith("/photo") || command == "/captura") {
        // Verificar si tiene argumento (número de foto)
        String args = "";
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex > 0) {
            args = command.substring(spaceIndex + 1);
            args.trim();
        }

        if (args.length() > 0) {
            // Enviar foto por número de ID
            int photoId = args.toInt();
            if (photoId < 1) {
                int total = sdCard.countAllPhotos();
                bot->sendMessage(chatId, "Uso: /foto N\n\nDonde N es el numero de foto (1-" + String(total) + ")\nUsa /carpeta para ver la lista", "");
            } else {
                if (!sdCard.isInitialized()) {
                    bot->sendMessage(chatId, "SD Card no disponible", "");
                } else {
                    String photoPath = sdCard.getPhotoPathByIndex(photoId);
                    if (photoPath.isEmpty()) {
                        int total = sdCard.countAllPhotos();
                        bot->sendMessage(chatId, "Foto #" + String(photoId) + " no encontrada.\nHay " + String(total) + " fotos. Usa /carpeta para ver la lista.", "");
                    } else {
                        bot->sendMessage(chatId, "📤 Enviando foto #" + String(photoId) + "...", "");
                        size_t photoSize = 0;
                        uint8_t* photoData = sdCard.readPhoto(photoPath, photoSize);
                        if (photoData && photoSize > 0) {
                            sendPhoto(photoData, photoSize, formatPhotoCaption(photoId, photoPath, photoSize));
                            sdCard.freePhotoBuffer(photoData);
                        } else {
                            bot->sendMessage(chatId, "Error al leer foto de SD", "");
                        }
                    }
                }
            }
        } else {
            // Sin argumentos: capturar foto actual
            bot->sendMessage(chatId, "📸 Capturando foto...", "");

            camera_fb_t* fb = camera.capturePhoto();
            if (fb) {
                // Guardar en SD en carpeta fotos_telegram
                if (sdCard.isInitialized()) {
                    struct tm timeinfo;
                    String filename;
                    if (getLocalTime(&timeinfo)) {
                        char buf[80];
                        snprintf(buf, sizeof(buf), "/%s/%04d-%02d-%02d_%02d-%02d-%02d.jpg",
                                 TELEGRAM_PHOTOS_FOLDER,
                                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                        filename = String(buf);
                    } else {
                        filename = "/" + String(TELEGRAM_PHOTOS_FOLDER) + "/foto_" + String(millis()) + ".jpg";
                    }
                    if (!SD_MMC.exists("/" + String(TELEGRAM_PHOTOS_FOLDER))) {
                        SD_MMC.mkdir("/" + String(TELEGRAM_PHOTOS_FOLDER));
                    }
                    sdCard.savePhoto(fb->buf, fb->len, filename);
                }

                // Construir caption con fecha/hora y peso
                String caption = "📷 Foto capturada";
                struct tm captureTime;
                if (getLocalTime(&captureTime)) {
                    char timeBuf[32];
                    strftime(timeBuf, sizeof(timeBuf), "%d/%m/%Y %H:%M:%S", &captureTime);
                    caption += "\n" + String(timeBuf);
                }
                // Mostrar peso de la foto
                if (fb->len >= 1024) {
                    caption += "\n⚖️ Peso: " + String(fb->len / 1024.0, 1) + " KB";
                } else {
                    caption += "\n⚖️ Peso: " + String(fb->len) + " bytes";
                }

                // Enviar por Telegram
                sendPhoto(fb->buf, fb->len, caption);
                camera.releaseFrame(fb);
            } else {
                bot->sendMessage(chatId, "Error al capturar la foto", "");
            }
        }
    }
    else if (command == "/estado" || command == "/status") {
        sendStatusMessage(chatId);
    }
    // Comando /flash on|off: requiere argumento explícito
    else if (command.startsWith("/flash")) {
        String args = "";
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex > 0) {
            args = command.substring(spaceIndex + 1);
            args.trim();
        }

        if (args == "on") {
            camera.setFlash(true);
            camera.saveSettings();
            dailyConfig.useFlash = true;
            saveDailyPhotoConfig();
            String msg = "⚡ Flash: ACTIVADO\n(Aplica a fotos y foto diaria)";
            bot->sendMessage(chatId, msg, "");
        } else if (args == "off") {
            camera.setFlash(false);
            camera.saveSettings();
            dailyConfig.useFlash = false;
            saveDailyPhotoConfig();
            String msg = "🌑 Flash: DESACTIVADO\n(Aplica a fotos y foto diaria)";
            bot->sendMessage(chatId, msg, "");
        } else {
            CameraSettings currentSettings = camera.getSettings();
            String estado = currentSettings.flashEnabled ? "ACTIVADO" : "DESACTIVADO";
            bot->sendMessage(chatId, "Uso: /flash on o /flash off\nEstado actual: " + estado, "");
        }
    }
    // Comando /fan on|off: controla ventilador en GPIO FAN_GPIO_NUM
    else if (command.startsWith("/fan")) {
        String args = "";
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex > 0) {
            args = command.substring(spaceIndex + 1);
            args.trim();
        }

        if (args == "on") {
            digitalWrite(FAN_GPIO_NUM, HIGH);
            bot->sendMessage(chatId, "💨 Ventilador: ENCENDIDO", "");
        } else if (args == "off") {
            digitalWrite(FAN_GPIO_NUM, LOW);
            bot->sendMessage(chatId, "🌬️ Ventilador: APAGADO", "");
        } else {
            bool isOn = digitalRead(FAN_GPIO_NUM) == HIGH;
            String estado = isOn ? "ENCENDIDO" : "APAGADO";
            bot->sendMessage(chatId, "Uso: /fan on o /fan off\nEstado actual: " + estado, "");
        }
    }
    // Comando para ver configuración de foto diaria
    else if (command == "/config" || command == "/configuracion") {
        sendDailyConfigMessage(chatId);
    }
    // Comando para configurar hora: /hora HH:MM
    else if (command.startsWith("/hora ") || command.startsWith("/sethour ") || command.startsWith("/settime ")) {
        // Parsear la hora del comando original (preservar mayúsculas no importa aquí)
        int spaceIndex = originalCommand.indexOf(' ');
        if (spaceIndex > 0) {
            String timeStr = originalCommand.substring(spaceIndex + 1);
            timeStr.trim();

            int colonIndex = timeStr.indexOf(':');
            if (colonIndex > 0) {
                int newHour = timeStr.substring(0, colonIndex).toInt();
                int newMinute = timeStr.substring(colonIndex + 1).toInt();

                if (newHour >= 0 && newHour <= 23 && newMinute >= 0 && newMinute <= 59) {
                    setDailyPhotoTime(newHour, newMinute);
                    saveDailyPhotoConfig();

                    String msg = "Hora de foto diaria actualizada a: ";
                    msg += String(newHour) + ":" + (newMinute < 10 ? "0" : "") + String(newMinute);
                    bot->sendMessage(chatId, msg, "");
                } else {
                    bot->sendMessage(chatId, "Hora invalida. Usa formato 24h (0-23:0-59)\nEjemplo: /hora 11:30", "");
                }
            } else {
                // Solo hora sin minutos
                int newHour = timeStr.toInt();
                if (newHour >= 0 && newHour <= 23) {
                    setDailyPhotoTime(newHour, 0);
                    saveDailyPhotoConfig();

                    String msg = "Hora de foto diaria actualizada a: ";
                    msg += String(newHour) + ":00";
                    bot->sendMessage(chatId, msg, "");
                } else {
                    bot->sendMessage(chatId, "Hora invalida. Usa formato 24h (0-23)\nEjemplo: /hora 11", "");
                }
            }
        } else {
            bot->sendMessage(chatId, "Uso: /hora HH:MM\nEjemplo: /hora 11:30", "");
        }
    }
    // Comando /fotodiaria con argumentos
    else if (command.startsWith("/fotodiaria")) {
        // Parsear argumentos
        String args = "";
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex > 0) {
            args = command.substring(spaceIndex + 1);
            args.trim();
        }

        if (args == "on") {
            // Activar envío automático
            dailyConfig.enabled = true;
            saveDailyPhotoConfig();
            String msg = "✅ Envio automatico de foto diaria: ACTIVADO\n";
            msg += "🕐 Proxima foto a las " + String(dailyConfig.hour) + ":" +
                   (dailyConfig.minute < 10 ? "0" : "") + String(dailyConfig.minute);
            bot->sendMessage(chatId, msg, "");
        }
        else if (args == "off") {
            // Desactivar envío automático
            dailyConfig.enabled = false;
            saveDailyPhotoConfig();
            bot->sendMessage(chatId, "⛔ Envio automatico de foto diaria: DESACTIVADO\n💾 (La foto se seguira guardando en SD)", "");
        }
        else if (args == "") {
            // Sin argumentos: enviar la foto del día guardada en SD
            bot->sendMessage(chatId, "📤 Enviando foto del dia guardada...", "");
            sendSavedDailyPhoto();
        }
        else {
            bot->sendMessage(chatId, "Uso: /fotodiaria [on|off]\n- Sin argumento: envia foto guardada en SD\n- on: activa envio automatico\n- off: desactiva envio automatico", "");
        }
    }
    // Comando /carpeta para listar fotos guardadas (TODAS las carpetas)
    else if (command.startsWith("/carpeta") || command.startsWith("/folder")) {
        if (!sdCard.isInitialized()) {
            bot->sendMessage(chatId, "SD Card no disponible", "");
        } else {
            // Parsear numero de pagina
            int page = 1;
            String args = "";
            int spaceIndex = command.indexOf(' ');
            if (spaceIndex > 0) {
                args = command.substring(spaceIndex + 1);
                args.trim();
                int parsed = args.toInt();
                if (parsed > 0) page = parsed;
            }

            int totalPages = 0;
            String list = sdCard.listAllPhotosTree(page, 10, &totalPages);

            // Corregir página si excede el total
            if (totalPages > 0 && page > totalPages) page = totalPages;

            if (list.isEmpty()) {
                bot->sendMessage(chatId, "No hay fotos guardadas en la SD", "");
            } else {
                String msg = "💾 SD Card - Todas las fotos:\n\n";
                msg += list;
                msg += "\n📄 Pag. " + String(page) + "/" + String(totalPages);
                if (totalPages > 1) {
                    msg += "  /carpeta N = otra pagina";
                }
                msg += "\n\n📤 Enviar foto: /enviar N";
                bot->sendMessage(chatId, msg, "");
            }
        }
    }
    // Comando /enviar N - enviar foto por número de la lista
    else if (command.startsWith("/enviar") || command.startsWith("/send")) {
        if (!sdCard.isInitialized()) {
            bot->sendMessage(chatId, "SD Card no disponible", "");
        } else {
            String args = "";
            int spaceIndex = command.indexOf(' ');
            if (spaceIndex > 0) {
                args = command.substring(spaceIndex + 1);
                args.trim();
            }

            int photoIndex = args.toInt();
            if (photoIndex < 1) {
                int total = sdCard.countAllPhotos();
                bot->sendMessage(chatId, "Uso: /enviar N\n\nDonde N es el numero de foto (1-" + String(total) + ")\nUsa /carpeta para ver la lista", "");
            } else {
                String photoPath = sdCard.getPhotoPathByIndex(photoIndex);
                if (photoPath.isEmpty()) {
                    int total = sdCard.countAllPhotos();
                    bot->sendMessage(chatId, "Foto #" + String(photoIndex) + " no encontrada.\nHay " + String(total) + " fotos. Usa /carpeta para ver la lista.", "");
                } else {
                    bot->sendMessage(chatId, "📤 Enviando foto #" + String(photoIndex) + "...", "");
                    size_t photoSize = 0;
                    uint8_t* photoData = sdCard.readPhoto(photoPath, photoSize);
                    if (photoData && photoSize > 0) {
                        sendPhoto(photoData, photoSize, formatPhotoCaption(photoIndex, photoPath, photoSize));
                        sdCard.freePhotoBuffer(photoData);
                    } else {
                        bot->sendMessage(chatId, "Error al leer foto de SD", "");
                    }
                }
            }
        }
    }
    else if (command == "/stream") {
        String ip = WiFi.localIP().toString();
        String msg = "🎥 Streaming en:\nhttp://" + ip + "/stream\n\n🌐 Dashboard:\nhttp://" + ip + "/";
        bot->sendMessage(chatId, msg, "");
    }
    else if (command == "/ip") {
        String ip = WiFi.localIP().toString();
        bot->sendMessage(chatId, "🌐 IP: " + ip, "");
    }
    else if (command == "/reiniciar" || command == "/restart" || command == "/reboot") {
        bot->sendMessage(chatId, "🔄 Reiniciando ESP32-CAM...", "");
        delay(1000);
        ESP.restart();
    }
    // Comandos de gestión de usuarios (solo admin)
    else if (command.startsWith("/add ") || command.startsWith("/adduser ")) {
        if (!isAdmin(chatId)) {
            bot->sendMessage(chatId, "Solo el administrador puede agregar usuarios.", "");
            return;
        }

        String args = originalCommand.substring(originalCommand.indexOf(' ') + 1);
        args.trim();

        if (args == "" || args == "/add" || args == "/adduser") {
            bot->sendMessage(chatId, "Uso: /add ID\n\nEl usuario puede obtener su ID con @userinfobot", "");
        } else {
            if (isAuthorized(args)) {
                bot->sendMessage(chatId, "El ID " + args + " ya esta autorizado.", "");
            } else if (addAuthorizedId(args)) {
                bot->sendMessage(chatId, "Usuario " + args + " agregado.\nTotal: " + String(authorizedCount) + " usuarios", "");
                // Notificar al nuevo usuario
                bot->sendMessage(args, "✅ Has sido autorizado para usar este bot.\nUsa /ayuda para ver los comandos.", "");
            } else {
                bot->sendMessage(chatId, "No se pudo agregar. Maximo " + String(MAX_AUTHORIZED_IDS) + " usuarios.", "");
            }
        }
    }
    else if (command.startsWith("/remove ") || command.startsWith("/removeuser ") || command.startsWith("/del ")) {
        if (!isAdmin(chatId)) {
            bot->sendMessage(chatId, "Solo el administrador puede eliminar usuarios.", "");
            return;
        }

        String args = originalCommand.substring(originalCommand.indexOf(' ') + 1);
        args.trim();

        if (args == "" || args == "/remove" || args == "/del") {
            bot->sendMessage(chatId, "Uso: /remove ID\n\nUsa /users para ver la lista", "");
        } else if (args == chatId) {
            bot->sendMessage(chatId, "No puedes eliminarte a ti mismo (admin).", "");
        } else {
            if (removeAuthorizedId(args)) {
                bot->sendMessage(chatId, "Usuario " + args + " eliminado.\nTotal: " + String(authorizedCount) + " usuarios", "");
            } else {
                bot->sendMessage(chatId, "ID " + args + " no encontrado.", "");
            }
        }
    }
    else if (command.startsWith("/admin ")) {
        if (!isAdmin(chatId)) {
            bot->sendMessage(chatId, "Solo los administradores pueden usar este comando.", "");
            return;
        }

        String args = originalCommand.substring(originalCommand.indexOf(' ') + 1);
        args.trim();

        if (args == "" || args == "/admin") {
            bot->sendMessage(chatId, "Uso: /admin ID\n\nHace administrador a un usuario autorizado.\nLimite: " + String(MAX_ADMINS) + " admins.\nAdmins actuales: " + String(getAdminCount()) + "/" + String(MAX_ADMINS), "");
        } else if (!isAuthorized(args)) {
            bot->sendMessage(chatId, "El ID " + args + " no es un usuario autorizado.\nPrimero usa /add " + args, "");
        } else if (isAdmin(args)) {
            bot->sendMessage(chatId, "El usuario " + args + " ya es administrador.", "");
        } else if (getAdminCount() >= MAX_ADMINS) {
            bot->sendMessage(chatId, "Limite de administradores alcanzado (" + String(MAX_ADMINS) + "/" + String(MAX_ADMINS) + ").\nNo se pueden agregar mas admins.", "");
        } else {
            if (makeAdmin(args)) {
                bot->sendMessage(chatId, "Usuario " + args + " ahora es administrador.\nAdmins: " + String(getAdminCount()) + "/" + String(MAX_ADMINS), "");
                bot->sendMessage(args, "👑 Ahora eres administrador del bot.\nPuedes usar /add, /remove y /admin.", "");
            } else {
                bot->sendMessage(chatId, "Error al hacer admin al usuario.", "");
            }
        }
    }
    else if (command == "/users" || command == "/ids") {
        String list = getAuthorizedIdsList();
        String msg = "👥 Usuarios (" + String(authorizedCount) + "/" + String(MAX_AUTHORIZED_IDS) + "):\n\n";
        msg += list;
        if (isAdmin(chatId)) {
            msg += "\n/add ID - Agregar\n/remove ID - Eliminar\n/admin ID - Hacer admin";
        }
        bot->sendMessage(chatId, msg, "");
    }
    else if (command == "/myid") {
        bot->sendMessage(chatId, "🆔 Tu ID: " + chatId, "");
    }
    // ----- MODO SLEEP -----
    else if (command == "/dormir" || command == "/sleep" ||
             command.startsWith("/dormir ") || command.startsWith("/sleep ")) {
        // Argumento opcional: minutos de timeout de inactividad
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex > 0) {
            String args = originalCommand.substring(spaceIndex + 1);
            args.trim();
            int mins = args.toInt();
            if (mins < 0 || mins > 1440) {
                bot->sendMessage(chatId, "Valor invalido. Usa /dormir N (0-1440 minutos).", "");
                return;
            }
            sleepManager.setTimeout(mins == 0 ? 0 : (unsigned long)mins * 60000UL);
            sleepManager.saveTimeout();
        }
        String msg = "😴 Entrando en modo sleep.\n";
        msg += "🔋 Consumo reducido. Poll Telegram cada " + String(sleepManager.getSleepPollInterval() / 1000UL) + " s.\n";
        msg += "💬 Escribe cualquier comando o conéctate al dashboard para activarme.";
        bot->sendMessage(chatId, msg, "");
        sleepManager.enterSleep();
    }
    else if (command == "/despertar" || command == "/wake") {
        if (sleepManager.isSleeping()) {
            sleepManager.exitSleep();
            bot->sendMessage(chatId, "⚡ Sistema activo!\n\n" + sleepManager.getStatus(), "");
        } else {
            bot->sendMessage(chatId, "Ya estoy activo.\n\n" + sleepManager.getStatus(), "");
        }
    }
    else if (command == "/sleepconfig" || command.startsWith("/sleepconfig ")) {
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex < 0) {
            // Sin argumentos: mostrar config actual
            bot->sendMessage(chatId, sleepManager.getStatus(), "");
        } else {
            String args = originalCommand.substring(spaceIndex + 1);
            args.trim();
            String argsLower = args;
            argsLower.toLowerCase();

            if (argsLower.startsWith("poll ")) {
                // /sleepconfig poll N → cambiar intervalo de poll en sleep (segundos)
                String pollArg = args.substring(5);
                pollArg.trim();
                int secs = pollArg.toInt();
                if (secs < 1 || secs > 300) {
                    bot->sendMessage(chatId, "Valor invalido. Usa /sleepconfig poll N (1-300 segundos).", "");
                } else {
                    sleepManager.setSleepPollInterval((unsigned long)secs * 1000UL);
                    sleepManager.saveSleepPollInterval();
                    bot->sendMessage(chatId, "Poll de Telegram en sleep: " + String(secs) + " s\n\n" + sleepManager.getStatus(), "");
                }
            } else if (argsLower == "off" || argsLower == "0") {
                // /sleepconfig off o /sleepconfig 0 → desactivar auto-sleep
                sleepManager.setTimeout(0);
                sleepManager.saveTimeout();
                bot->sendMessage(chatId, "Auto-sleep desactivado.\n\n" + sleepManager.getStatus(), "");
            } else {
                // /sleepconfig N → timeout de inactividad en minutos
                int mins = args.toInt();
                if (mins < 1 || mins > 1440) {
                    bot->sendMessage(chatId, "Uso:\n/sleepconfig - Ver estado\n/sleepconfig N - Timeout (1-1440 min)\n/sleepconfig off - Desactivar auto-sleep\n/sleepconfig poll N - Poll en sleep (1-300 s)", "");
                } else {
                    sleepManager.setTimeout((unsigned long)mins * 60000UL);
                    sleepManager.saveTimeout();
                    bot->sendMessage(chatId, "Timeout de inactividad: " + String(mins) + " min\n\n" + sleepManager.getStatus(), "");
                }
            }
        }
    }
    // ----- MODO AUTORIZACIÓN TEMPORAL -----
    else if (command == "/acceso" || command.startsWith("/acceso ")) {
        if (!isAdmin(chatId)) {
            bot->sendMessage(chatId, "🔒 Solo los administradores pueden usar este comando.", "");
            return;
        }

        String args = "";
        int spaceIndex = command.indexOf(' ');
        if (spaceIndex > 0) {
            args = command.substring(spaceIndex + 1);
            args.trim();
        }

        if (args == "") {
            // Mostrar estado actual
            String msg = "🔓 Modo autorización temporal: ";
            if (tempAuthMode) {
                msg += "*ACTIVO*\n";
                if (tempAuthExpiry > 0) {
                    unsigned long remaining = (tempAuthExpiry - millis()) / 1000;
                    msg += "⏱️ Expira en: " + String(remaining / 60) + " min " + String(remaining % 60) + " s\n";
                } else {
                    msg += "Sin límite de tiempo.\n";
                }
                msg += "Cualquier usuario que escriba quedará autorizado.\nUsa /acceso off para desactivar.";
            } else {
                msg += "*INACTIVO*\n";
                msg += "Usa /acceso on para activar.";
            }
            bot->sendMessage(chatId, msg, "");
        }
        else if (args == "on") {
            tempAuthMode = true;
            tempAuthExpiry = 0;
            bot->sendMessage(chatId, "🔓 Modo autorización temporal ACTIVADO.\nCualquier usuario que escriba al bot quedará autorizado automáticamente.\nUsa /acceso off para desactivar.", "");
        }
        else if (args == "off") {
            tempAuthMode = false;
            tempAuthExpiry = 0;
            bot->sendMessage(chatId, "🔒 Modo autorización temporal DESACTIVADO.\nNo se autorizarán nuevos usuarios automáticamente.", "");
        }
        else {
            // Intentar parsear como minutos
            int mins = args.toInt();
            if (mins >= 1 && mins <= 1440) {
                tempAuthMode = true;
                tempAuthExpiry = millis() + (unsigned long)mins * 60000UL;
                String msg = "🔓 Modo autorización temporal ACTIVADO por " + String(mins) + " minuto";
                if (mins != 1) msg += "s";
                msg += ".\nSe desactivará automáticamente. Usa /acceso off para cancelar antes.";
                bot->sendMessage(chatId, msg, "");
            } else {
                bot->sendMessage(chatId, "Uso:\n/acceso - Ver estado\n/acceso on - Activar (sin límite)\n/acceso off - Desactivar\n/acceso N - Activar por N minutos (1–1440)", "");
            }
        }
    }
    else {
        bot->sendMessage(chatId, "Comando no reconocido. Usa /ayuda", "");
    }
}

void TelegramBot::sendHelpMessage(String chatId) {
    String helpMsg = "📋 Comandos disponibles:\n\n";
    helpMsg += "📸 FOTOS:\n";
    helpMsg += "/foto - Capturar y enviar foto\n";
    helpMsg += "/foto N - Enviar foto por numero\n";
    helpMsg += "/carpeta - Ver todas las fotos guardadas\n";
    helpMsg += "/enviar N - Enviar foto N de la lista\n\n";

    helpMsg += "⚡ FLASH:\n";
    helpMsg += "/flash on - Activar flash\n";
    helpMsg += "/flash off - Desactivar flash\n";
    helpMsg += "(Aplica a fotos y foto diaria)\n\n";

    helpMsg += "💨 VENTILADOR:\n";
    helpMsg += "/fan on - Encender ventilador\n";
    helpMsg += "/fan off - Apagar ventilador\n";
    helpMsg += "/fan - Ver estado del ventilador\n\n";

    helpMsg += "📅 FOTO DIARIA:\n";
    helpMsg += "/fotodiaria - Enviar foto del dia guardada\n";
    helpMsg += "/fotodiaria on/off - Activar/desactivar envio\n";
    helpMsg += "/config - Ver configuracion actual\n";
    helpMsg += "/hora HH:MM - Cambiar hora\n\n";

    helpMsg += "👥 USUARIOS:\n";
    helpMsg += "/users - Ver autorizados\n";
    helpMsg += "/myid - Ver tu ID\n";
    if (isAdmin(chatId)) {
        helpMsg += "/add ID - Agregar usuario\n";
        helpMsg += "/remove ID - Eliminar usuario\n";
        helpMsg += "/admin ID - Hacer administrador (max " + String(MAX_ADMINS) + ")\n";
        helpMsg += "/acceso - Modo autorización temporal\n";
        helpMsg += "/acceso on/off - Activar/desactivar\n";
        helpMsg += "/acceso N - Activar por N minutos\n";
    }
    helpMsg += "\n";

    helpMsg += "📊 SISTEMA:\n";
    helpMsg += "/estado - Ver estado del sistema\n";
    helpMsg += "/stream - Ver enlace de streaming\n";
    helpMsg += "/ip - Ver direccion IP\n";
    helpMsg += "/reiniciar - Reiniciar ESP32-CAM\n\n";

    helpMsg += "🔋 AHORRO DE ENERGIA:\n";
    helpMsg += "/dormir - Entrar en modo sleep\n";
    helpMsg += "/dormir N - Sleep y cambiar timeout a N min\n";
    helpMsg += "/despertar - Salir del modo sleep\n";
    helpMsg += "/sleepconfig - Ver configuracion de sleep\n";
    helpMsg += "/sleepconfig N - Timeout inactividad (min)\n";
    helpMsg += "/sleepconfig off - Desactivar auto-sleep\n";
    helpMsg += "/sleepconfig poll N - Poll en sleep (seg)";

    bot->sendMessage(chatId, helpMsg, "");
}

void TelegramBot::sendStatusMessage(String chatId) {
    String status = "📊 Estado del Sistema:\n\n";

    // Memoria
    status += "🔋 RAM libre: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
    status += "💾 PSRAM libre: " + String(ESP.getFreePsram() / 1024) + " KB\n";

    // WiFi
    status += "📶 WiFi RSSI: " + String(WiFi.RSSI()) + " dBm\n";
    status += "🌐 IP: " + WiFi.localIP().toString() + "\n";

    // SD Card
    if (sdCard.isInitialized()) {
        float sdFreeGB = sdCard.getFreeSpace() / (1024.0 * 1024.0 * 1024.0);
        float sdTotalGB = sdCard.getTotalSpace() / (1024.0 * 1024.0 * 1024.0);
        status += "💿 SD: " + String(sdFreeGB, 1) + "/" + String(sdTotalGB, 1) + " GB Libres\n";
        status += "📁 Carpeta: /" + sdCard.getPhotosFolder() + "\n";
    } else {
        status += "💿 SD: No disponible\n";
    }

    // Configuración de cámara
    CameraSettings settings = camera.getSettings();
    status += "\n📷 Configuracion de Camara:\n";
    status += "⚡ Flash: " + String(settings.flashEnabled ? "ON" : "OFF") + "\n";
    status += "☀️ Brillo: " + String(settings.brightness) + "\n";
    status += "🌓 Contraste: " + String(settings.contrast) + "\n";
    status += "🎞️ Calidad: " + String(settings.quality) + "\n";

    // Configuración de foto diaria
    status += "\n📅 Foto Diaria (a las " + String(dailyConfig.hour) + ":" +
              (dailyConfig.minute < 10 ? "0" : "") + String(dailyConfig.minute) + "):\n";
    status += "📨 Envio Telegram: " + String(dailyConfig.enabled ? "ON" : "OFF") + "\n";
    status += "💾 Guardar SD: SIEMPRE\n";

    // Modo sleep
    status += "\n" + sleepManager.getStatus();

    bot->sendMessage(chatId, status, "");
}

void TelegramBot::sendDailyConfigMessage(String chatId) {
    String msg = "📅 Configuracion de Foto Diaria:\n\n";
    msg += "🕐 Hora programada: " + String(dailyConfig.hour) + ":" +
           (dailyConfig.minute < 10 ? "0" : "") + String(dailyConfig.minute) + "\n";
    msg += "📨 Envio automatico: " + String(dailyConfig.enabled ? "✅ ACTIVADO" : "⛔ DESACTIVADO") + "\n";
    msg += "💾 Guardar en SD: SIEMPRE\n";
    msg += "⚡ Flash: " + String(dailyConfig.useFlash ? "✅ ACTIVADO" : "⛔ DESACTIVADO") + "\n";

    // Verificar si hay foto guardada hoy
    if (sdCard.isInitialized() && sdCard.photoExistsToday()) {
        msg += "📸 Foto de hoy: ✅ GUARDADA\n";
    } else {
        msg += "📸 Foto de hoy: ❌ NO DISPONIBLE\n";
    }

    msg += "\n📋 Comandos:\n";
    msg += "/foto - Tomar foto ahora\n";
    msg += "/fotodiaria - Ver foto guardada\n";
    msg += "/fotodiaria on/off - Envio automatico\n";
    msg += "/hora HH:MM - Cambiar hora\n";
    msg += "/flash on|off - Activar/desactivar flash";

    bot->sendMessage(chatId, msg, "");
}

// Envío directo de foto via HTTP POST multipart a Telegram API
// Reemplaza sendPhotoByBinary que falla en ESP32-CAM
bool TelegramBot::sendPhotoToChat(const uint8_t* imageData, size_t imageSize, String chatId, String caption) {
    String token = credentialsManager.getBotToken();
    String boundary = "----ESP32CAMBoundary";

    // Construir cabecera multipart
    String head = "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
    head += chatId + "\r\n";

    if (caption.length() > 0) {
        head += "--" + boundary + "\r\n";
        head += "Content-Disposition: form-data; name=\"caption\"\r\n\r\n";
        head += caption + "\r\n";
    }

    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n";
    head += "Content-Type: image/jpeg\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";

    size_t totalLen = head.length() + imageSize + tail.length();

    WiFiClientSecure sendClient;
    sendClient.setInsecure();
    sendClient.setTimeout(20);

    Serial.printf("Conectando a Telegram API para chat %s...\n", chatId.c_str());

    if (!sendClient.connect("api.telegram.org", 443)) {
        Serial.println("Error conectando a api.telegram.org");
        return false;
    }

    // Enviar request HTTP POST
    sendClient.println("POST /bot" + token + "/sendPhoto HTTP/1.1");
    sendClient.println("Host: api.telegram.org");
    sendClient.println("Content-Length: " + String(totalLen));
    sendClient.println("Content-Type: multipart/form-data; boundary=" + boundary);
    sendClient.println("Connection: close");
    sendClient.println();

    // Enviar cabecera multipart
    sendClient.print(head);

    // Enviar datos de imagen en chunks de 1024 bytes
    size_t sent = 0;
    while (sent < imageSize) {
        size_t chunk = imageSize - sent;
        if (chunk > 1024) chunk = 1024;
        size_t written = sendClient.write(imageData + sent, chunk);
        if (written == 0) {
            Serial.println("Error escribiendo datos de foto");
            sendClient.stop();
            return false;
        }
        sent += written;
        delay(1); // Yield para watchdog
    }

    // Enviar cola multipart
    sendClient.print(tail);

    // Leer respuesta con timeout
    unsigned long timeout = millis() + 15000;
    while (!sendClient.available() && millis() < timeout) {
        delay(10);
    }

    String response = "";
    while (sendClient.available()) {
        response += (char)sendClient.read();
    }

    sendClient.stop();

    bool success = response.indexOf("\"ok\":true") >= 0;
    if (success) {
        Serial.println("Foto enviada a: " + chatId);
    } else {
        Serial.println("Error respuesta Telegram: " + response.substring(0, 200));
    }
    return success;
}

bool TelegramBot::sendPhoto(const uint8_t* imageData, size_t imageSize, String caption) {
    if (authorizedCount == 0) return false;

    Serial.printf("Enviando foto por Telegram (%d bytes) a %d usuarios...\n", imageSize, authorizedCount);

    bool anySuccess = false;

    for (int i = 0; i < authorizedCount; i++) {
        if (sendPhotoToChat(imageData, imageSize, authorizedIds[i], caption)) {
            anySuccess = true;
        }
    }

    return anySuccess;
}

bool TelegramBot::sendMessage(String message) {
    if (!bot || authorizedCount == 0) return false;

    bool anySuccess = false;

    // Enviar a todos los usuarios autorizados
    for (int i = 0; i < authorizedCount; i++) {
        if (bot->sendMessage(authorizedIds[i], message, "")) {
            anySuccess = true;
        }
    }

    return anySuccess;
}

bool TelegramBot::takeDailyPhoto(bool sendToTelegram) {
    // Despertar el sistema antes de enviar para garantizar WiFi a plena potencia
    sleepManager.registerActivity();

    // Flash se maneja en capturePhoto() segun settings.flashEnabled
    camera_fb_t* fb = camera.capturePhoto();

    if (!fb) {
        if (sendToTelegram) {
            sendMessage("Error al capturar foto del dia");
        }
        Serial.println("Error al capturar foto del dia");
        return false;
    }

    // Siempre guardar como foto del día en SD
    bool savedToSD = false;
    if (sdCard.isInitialized()) {
        String dailyPath = sdCard.getDailyPhotoPath();
        savedToSD = sdCard.savePhoto(fb->buf, fb->len, dailyPath);
        if (savedToSD) {
            Serial.println("Foto del dia guardada en SD: " + dailyPath);
        }
    }

    // Enviar a Telegram solo si se solicita
    bool sentToTelegram = false;
    if (sendToTelegram) {
        // Obtener fecha actual para el caption
        struct tm timeinfo;
        String dateStr = "Foto del dia";
        if (getLocalTime(&timeinfo)) {
            char buffer[32];
            strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M", &timeinfo);
            dateStr = "Foto del dia: " + String(buffer);
            if (camera.getSettings().flashEnabled) {
                dateStr += " (con flash)";
            }
        }

        sentToTelegram = sendPhoto(fb->buf, fb->len, dateStr);
    }

    camera.releaseFrame(fb);

    // Retornar true si al menos una operación fue exitosa
    return savedToSD || sentToTelegram;
}

bool TelegramBot::sendDailyPhoto() {
    // Envía la foto diaria guardada en SD
    return sendSavedDailyPhoto();
}

bool TelegramBot::sendSavedDailyPhoto() {
    // Verificar si hay SD
    if (!sdCard.isInitialized()) {
        sendMessage("SD Card no disponible");
        return false;
    }

    // Verificar si existe foto del día
    if (!sdCard.photoExistsToday()) {
        sendMessage("No hay foto del dia guardada.\nLa foto se toma automaticamente a las " +
                    String(dailyConfig.hour) + ":" +
                    (dailyConfig.minute < 10 ? "0" : "") + String(dailyConfig.minute));
        return false;
    }

    // Leer foto de la SD
    String dailyPath = sdCard.getDailyPhotoPath();
    size_t photoSize = 0;
    uint8_t* photoData = sdCard.readPhoto(dailyPath, photoSize);

    if (!photoData || photoSize == 0) {
        sendMessage("Error al leer foto del dia desde SD");
        return false;
    }

    // Obtener fecha actual para el caption
    struct tm timeinfo;
    String dateStr = "Foto del dia (guardada)";
    if (getLocalTime(&timeinfo)) {
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%d/%m/%Y", &timeinfo);
        dateStr = "Foto del dia: " + String(buffer);
    }

    // Enviar por Telegram
    bool success = sendPhoto(photoData, photoSize, dateStr);

    // Liberar memoria
    sdCard.freePhotoBuffer(photoData);

    return success;
}

void TelegramBot::setCheckInterval(unsigned long interval) {
    checkInterval = interval;
}

void TelegramBot::setDailyPhotoTime(int hour, int minute) {
    dailyConfig.hour = constrain(hour, 0, 23);
    dailyConfig.minute = constrain(minute, 0, 59);
    Serial.printf("Hora de foto diaria cambiada a: %02d:%02d\n", dailyConfig.hour, dailyConfig.minute);
}

void TelegramBot::setDailyPhotoFlash(bool useFlash) {
    dailyConfig.useFlash = useFlash;
    Serial.printf("Flash para foto diaria: %s\n", useFlash ? "ON" : "OFF");
}

DailyPhotoConfig TelegramBot::getDailyPhotoConfig() {
    return dailyConfig;
}

void TelegramBot::saveDailyPhotoConfig() {
    dailyPrefs.begin("dailyphoto", false);
    dailyPrefs.putInt("hour", dailyConfig.hour);
    dailyPrefs.putInt("minute", dailyConfig.minute);
    dailyPrefs.putBool("flash", dailyConfig.useFlash);
    dailyPrefs.putBool("enabled", dailyConfig.enabled);
    dailyPrefs.end();
    Serial.println("Configuracion de foto diaria guardada");
}

void TelegramBot::loadDailyPhotoConfig() {
    dailyPrefs.begin("dailyphoto", true);
    dailyConfig.hour = dailyPrefs.getInt("hour", DAILY_PHOTO_HOUR);
    dailyConfig.minute = dailyPrefs.getInt("minute", DAILY_PHOTO_MINUTE);
    dailyConfig.useFlash = dailyPrefs.getBool("flash", DAILY_PHOTO_FLASH);
    dailyConfig.enabled = dailyPrefs.getBool("enabled", DAILY_PHOTO_ENABLED);
    dailyPrefs.end();
    Serial.printf("Configuracion cargada: %s - %02d:%02d, Flash: %s\n",
                  dailyConfig.enabled ? "ACTIVA" : "INACTIVA",
                  dailyConfig.hour, dailyConfig.minute,
                  dailyConfig.useFlash ? "ON" : "OFF");
}

// ============================================
// GESTIÓN DE USUARIOS AUTORIZADOS
// ============================================

bool TelegramBot::isAuthorized(String chatId) {
    for (int i = 0; i < authorizedCount; i++) {
        if (authorizedIds[i] == chatId) {
            return true;
        }
    }
    return false;
}

bool TelegramBot::isAdmin(String chatId) {
    for (int i = 0; i < authorizedCount; i++) {
        if (authorizedIds[i] == chatId && adminFlags[i]) {
            return true;
        }
    }
    return false;
}

bool TelegramBot::makeAdmin(String chatId) {
    // Verificar límite de admins
    if (getAdminCount() >= MAX_ADMINS) {
        return false;
    }

    // Buscar al usuario y hacerlo admin
    for (int i = 0; i < authorizedCount; i++) {
        if (authorizedIds[i] == chatId) {
            adminFlags[i] = true;
            saveAuthorizedIds();
            Serial.println("Nuevo admin: " + chatId);
            return true;
        }
    }
    return false;
}

int TelegramBot::getAdminCount() {
    int count = 0;
    for (int i = 0; i < authorizedCount; i++) {
        if (adminFlags[i]) count++;
    }
    return count;
}

bool TelegramBot::addAuthorizedId(String chatId) {
    // Verificar si ya existe
    if (isAuthorized(chatId)) {
        return false;
    }

    // Verificar límite
    if (authorizedCount >= MAX_AUTHORIZED_IDS) {
        return false;
    }

    // Agregar al final
    authorizedIds[authorizedCount] = chatId;
    // El primer usuario es admin automáticamente
    if (authorizedCount == 0) {
        adminFlags[authorizedCount] = true;
    } else {
        adminFlags[authorizedCount] = false;
    }
    authorizedCount++;

    // Guardar en memoria
    saveAuthorizedIds();

    Serial.println("Usuario autorizado agregado: " + chatId);
    return true;
}

bool TelegramBot::removeAuthorizedId(String chatId) {
    // Buscar el ID
    int foundIndex = -1;
    for (int i = 0; i < authorizedCount; i++) {
        if (authorizedIds[i] == chatId) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex < 0) {
        return false;
    }

    // Mover todos los elementos después del encontrado
    for (int i = foundIndex; i < authorizedCount - 1; i++) {
        authorizedIds[i] = authorizedIds[i + 1];
        adminFlags[i] = adminFlags[i + 1];
    }

    // Limpiar el último y decrementar contador
    authorizedIds[authorizedCount - 1] = "";
    adminFlags[authorizedCount - 1] = false;
    authorizedCount--;

    // Guardar en memoria
    saveAuthorizedIds();

    Serial.println("Usuario eliminado: " + chatId);
    return true;
}

String TelegramBot::getAuthorizedIdsList() {
    String list = "";
    for (int i = 0; i < authorizedCount; i++) {
        list += String(i + 1) + ". " + authorizedIds[i];
        if (adminFlags[i]) {
            list += " (Admin)";
        }
        list += "\n";
    }
    return list;
}

int TelegramBot::getAuthorizedCount() {
    return authorizedCount;
}

void TelegramBot::loadAuthorizedIds() {
    authPrefs.begin("authids", true);
    authorizedCount = authPrefs.getInt("count", 0);

    // Limitar al máximo permitido
    if (authorizedCount > MAX_AUTHORIZED_IDS) {
        authorizedCount = MAX_AUTHORIZED_IDS;
    }

    // Cargar cada ID y su flag de admin
    for (int i = 0; i < authorizedCount; i++) {
        String key = "id" + String(i);
        authorizedIds[i] = authPrefs.getString(key.c_str(), "");
        String adminKey = "adm" + String(i);
        adminFlags[i] = authPrefs.getBool(adminKey.c_str(), i == 0);  // Por defecto, el primero es admin
    }

    authPrefs.end();

    Serial.printf("IDs autorizados cargados: %d\n", authorizedCount);
    for (int i = 0; i < authorizedCount; i++) {
        Serial.printf("  [%d] %s%s\n", i, authorizedIds[i].c_str(), adminFlags[i] ? " (Admin)" : "");
    }
}

void TelegramBot::saveAuthorizedIds() {
    authPrefs.begin("authids", false);
    authPrefs.putInt("count", authorizedCount);

    // Guardar cada ID y su flag de admin
    for (int i = 0; i < authorizedCount; i++) {
        String key = "id" + String(i);
        authPrefs.putString(key.c_str(), authorizedIds[i]);
        String adminKey = "adm" + String(i);
        authPrefs.putBool(adminKey.c_str(), adminFlags[i]);
    }

    // Limpiar IDs sobrantes (si se eliminaron)
    for (int i = authorizedCount; i < MAX_AUTHORIZED_IDS; i++) {
        String key = "id" + String(i);
        authPrefs.remove(key.c_str());
        String adminKey = "adm" + String(i);
        authPrefs.remove(adminKey.c_str());
    }

    authPrefs.end();

    Serial.printf("IDs autorizados guardados: %d\n", authorizedCount);
}
