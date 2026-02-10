#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "config.h"

// Máximo de usuarios autorizados
#define MAX_AUTHORIZED_IDS 10

// Estructura para configuración de foto diaria
struct DailyPhotoConfig {
    int hour;
    int minute;
    bool useFlash;
    bool enabled;
};

class TelegramBot {
public:
    TelegramBot();

    void init();
    void handleMessages();
    bool sendPhoto(const uint8_t* imageData, size_t imageSize, String caption = "");
    bool sendPhotoToChat(const uint8_t* imageData, size_t imageSize, String chatId, String caption = "");
    bool sendMessage(String message);
    bool sendDailyPhoto();                        // Envía la foto diaria guardada en SD
    bool takeDailyPhoto(bool sendToTelegram);     // Toma foto, guarda en SD, envía a Telegram si se indica
    bool sendSavedDailyPhoto();                   // Envía la foto diaria ya guardada en SD

    void setCheckInterval(unsigned long interval);

    // Configuración de foto diaria
    void setDailyPhotoTime(int hour, int minute);
    void setDailyPhotoFlash(bool useFlash);
    DailyPhotoConfig getDailyPhotoConfig();
    void saveDailyPhotoConfig();
    void loadDailyPhotoConfig();

    // Gestión de usuarios autorizados
    bool isAuthorized(String chatId);
    bool isAdmin(String chatId);
    bool addAuthorizedId(String chatId);
    bool removeAuthorizedId(String chatId);
    String getAuthorizedIdsList();
    int getAuthorizedCount();

private:
    WiFiClientSecure client;
    UniversalTelegramBot* bot;
    unsigned long lastCheckTime;
    unsigned long checkInterval;

    // Configuración de foto diaria
    DailyPhotoConfig dailyConfig;

    // Lista de IDs autorizados (el primero es el admin)
    String authorizedIds[MAX_AUTHORIZED_IDS];
    int authorizedCount;

    void processMessage(telegramMessage& msg);
    void handleCommand(String command, String chatId);
    void sendHelpMessage(String chatId);
    void sendStatusMessage(String chatId);
    void sendDailyConfigMessage(String chatId);

    // Gestión interna de IDs
    void loadAuthorizedIds();
    void saveAuthorizedIds();
};

extern TelegramBot telegramBot;

#endif // TELEGRAM_BOT_H
