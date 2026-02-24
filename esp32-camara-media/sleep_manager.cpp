#include "sleep_manager.h"
#include "telegram_bot.h"
#include "recording_handler.h"
#include "config.h"
#include <WiFi.h>
#include <Preferences.h>

SleepManager sleepManager;

static Preferences sleepPrefs;

SleepManager::SleepManager()
    : sleeping(false),
      lastActivityTime(0),
      inactivityTimeout(SLEEP_INACTIVITY_TIMEOUT_DEFAULT),
      sleepPollInterval(SLEEP_TELEGRAM_INTERVAL) {
}

void SleepManager::begin() {
    lastActivityTime = millis();
    loadTimeout();
    loadSleepPollInterval();
    Serial.printf("[Sleep] Modo sleep listo. Timeout: %lu min | Poll sleep: %lu s\n",
                  inactivityTimeout / 60000UL,
                  sleepPollInterval / 1000UL);
}

void SleepManager::registerActivity() {
    lastActivityTime = millis();
    if (sleeping) {
        exitSleep();
    }
}

void SleepManager::enterSleep() {
    if (sleeping) return;
    sleeping = true;
    applyPowerMode();
    Serial.printf("[Sleep] Entrando en modo sleep. Idle: %lu s | Poll Telegram: %lu s\n",
                  getIdleSeconds(), sleepPollInterval / 1000UL);
}

void SleepManager::exitSleep() {
    if (!sleeping) return;
    sleeping = false;
    applyPowerMode();
    Serial.println("[Sleep] Saliendo del modo sleep. Sistema activo.");
}

bool SleepManager::isSleeping() const {
    return sleeping;
}

void SleepManager::checkAutoSleep() {
    if (sleeping) return;
    if (inactivityTimeout == 0) return;  // auto-sleep desactivado
    if (recordingHandler.isRecording()) return;  // no dormir durante grabacion
    if (millis() - lastActivityTime >= inactivityTimeout) {
        Serial.printf("[Sleep] Inactividad de %lu min → entrando en modo sleep.\n",
                      inactivityTimeout / 60000UL);
        enterSleep();
    }
}

void SleepManager::setTimeout(unsigned long timeoutMs) {
    inactivityTimeout = timeoutMs;
}

unsigned long SleepManager::getTimeout() const {
    return inactivityTimeout;
}

void SleepManager::saveTimeout() {
    sleepPrefs.begin("sleep", false);
    sleepPrefs.putULong("timeout", inactivityTimeout);
    sleepPrefs.end();
    Serial.printf("[Sleep] Timeout guardado: %lu ms\n", inactivityTimeout);
}

void SleepManager::loadTimeout() {
    sleepPrefs.begin("sleep", true);
    inactivityTimeout = sleepPrefs.getULong("timeout", SLEEP_INACTIVITY_TIMEOUT_DEFAULT);
    sleepPrefs.end();
}

void SleepManager::setSleepPollInterval(unsigned long intervalMs) {
    sleepPollInterval = intervalMs;
    // Si actualmente esta en sleep, aplicar inmediatamente
    if (sleeping) {
        telegramBot.setCheckInterval(sleepPollInterval);
    }
}

unsigned long SleepManager::getSleepPollInterval() const {
    return sleepPollInterval;
}

void SleepManager::saveSleepPollInterval() {
    sleepPrefs.begin("sleep", false);
    sleepPrefs.putULong("poll", sleepPollInterval);
    sleepPrefs.end();
    Serial.printf("[Sleep] Poll interval guardado: %lu ms\n", sleepPollInterval);
}

void SleepManager::loadSleepPollInterval() {
    sleepPrefs.begin("sleep", true);
    sleepPollInterval = sleepPrefs.getULong("poll", SLEEP_TELEGRAM_INTERVAL);
    sleepPrefs.end();
}

String SleepManager::getStatus() const {
    String s = "";
    s += "Modo sleep: " + String(sleeping ? "ACTIVO" : "INACTIVO") + "\n";

    if (inactivityTimeout == 0) {
        s += "Auto-sleep: DESACTIVADO\n";
    } else {
        s += "Auto-sleep tras: " + String(inactivityTimeout / 60000UL) + " min\n";
    }

    if (!sleeping) {
        s += "Idle actual: " + String(getIdleSeconds()) + " s\n";
    }

    s += "Poll Telegram en sleep: " + String(sleepPollInterval / 1000UL) + " s";
    return s;
}

unsigned long SleepManager::getIdleSeconds() const {
    return (millis() - lastActivityTime) / 1000UL;
}

void SleepManager::applyPowerMode() {
    if (sleeping) {
        // Activar WiFi modem sleep (ahorra ~40-50% consumo WiFi)
        WiFi.setSleep(true);
        // Reducir frecuencia de polling de Telegram
        telegramBot.setCheckInterval(sleepPollInterval);
    } else {
        // Restaurar WiFi full-power
        WiFi.setSleep(false);
        // Restaurar polling normal de Telegram
        telegramBot.setCheckInterval(TELEGRAM_CHECK_INTERVAL);
    }
}
