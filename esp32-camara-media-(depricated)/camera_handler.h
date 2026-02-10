#ifndef CAMERA_HANDLER_H
#define CAMERA_HANDLER_H

#include <Arduino.h>
#include "esp_camera.h"

// Estructura para guardar configuración de la cámara
struct CameraSettings {
    int brightness;      // -2 a 2
    int contrast;        // -2 a 2
    int saturation;      // -2 a 2
    int specialEffect;   // 0-6
    int whiteBalance;    // 0-4
    int exposureCtrl;    // 0 o 1
    int aecValue;        // 0-1200
    int gainCtrl;        // 0 o 1
    int agcGain;         // 0-30
    int quality;         // 10-63
    framesize_t frameSize; // Resolución
    bool flashEnabled;
};

class CameraHandler {
public:
    CameraHandler();

    bool init();
    camera_fb_t* capturePhoto(bool useFlash = true);
    void releaseFrame(camera_fb_t* fb);

    // Getters y setters de configuración
    void setBrightness(int value);
    void setContrast(int value);
    void setSaturation(int value);
    void setSpecialEffect(int effect);
    void setWhiteBalance(int mode);
    void setExposureCtrl(bool enable);
    void setAecValue(int value);
    void setGainCtrl(bool enable);
    void setAgcGain(int value);
    void setQuality(int value);
    void setFrameSize(framesize_t size);
    void setFlash(bool enable);

    CameraSettings getSettings();
    void applySettings(CameraSettings& settings);

    // Guardar/cargar configuración
    void saveSettings();
    void loadSettings();

private:
    CameraSettings settings;
    bool initialized;

    void setDefaultSettings();
};

extern CameraHandler camera;

#endif // CAMERA_HANDLER_H
