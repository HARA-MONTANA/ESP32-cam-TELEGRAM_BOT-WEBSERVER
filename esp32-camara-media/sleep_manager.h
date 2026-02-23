#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <Arduino.h>

/*
 * SleepManager - Modo ahorro de energia para ESP32-CAM
 *
 * En modo sleep:
 *   - WiFi modem sleep activado (WiFi.setSleep(true)) → ahorro ~40-50% de consumo
 *   - Polling de Telegram reducido (10s en lugar de 1s)
 *   - Web server sigue activo (cualquier conexion web despierta el sistema)
 *
 * Activacion:
 *   - Auto: tras N minutos sin actividad (configurable, 0 = desactivado)
 *   - Manual: via comando /dormir de Telegram
 *
 * Desactivacion:
 *   - Cualquier mensaje de Telegram (registerActivity())
 *   - Cualquier conexion web (registerActivity() desde web_server)
 *   - Comando /despertar
 */

class SleepManager {
public:
    SleepManager();

    // Inicializar: cargar configuracion guardada en NVS
    void begin();

    // Registrar actividad (resetea timer y despierta si estaba dormido)
    void registerActivity();

    // Control manual de sleep
    void enterSleep();
    void exitSleep();
    bool isSleeping() const;

    // Verificar y aplicar auto-sleep (llamar desde loop)
    void checkAutoSleep();

    // Configuracion de timeout de inactividad
    void setTimeout(unsigned long timeoutMs);  // 0 = auto-sleep desactivado
    unsigned long getTimeout() const;
    void saveTimeout();    // Persistir en NVS
    void loadTimeout();    // Leer de NVS

    // Intervalo de poll de Telegram en modo sleep
    void setSleepPollInterval(unsigned long intervalMs);
    unsigned long getSleepPollInterval() const;
    void saveSleepPollInterval();

    // Informacion de estado
    String getStatus() const;
    unsigned long getIdleSeconds() const;

private:
    bool sleeping;
    unsigned long lastActivityTime;
    unsigned long inactivityTimeout;    // ms, 0 = auto-sleep desactivado
    unsigned long sleepPollInterval;    // intervalo de Telegram en sleep (ms)

    void applyPowerMode();
};

extern SleepManager sleepManager;

#endif // SLEEP_MANAGER_H
