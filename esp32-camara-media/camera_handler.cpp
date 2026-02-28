#include "camera_handler.h"
#include "config.h"
#include <Preferences.h>

CameraHandler camera;
static Preferences prefs;

CameraHandler::CameraHandler() : initialized(false) {
    setDefaultSettings();
}

void CameraHandler::setDefaultSettings() {
    settings.brightness = 0;
    settings.contrast = 0;
    settings.saturation = 0;
    settings.specialEffect = 0;
    settings.whiteBalance = 0;
    settings.exposureCtrl = 1;
    settings.aecValue = 300;
    settings.gainCtrl = 1;
    settings.agcGain = 0;
    settings.quality = 12;
    settings.frameSize = FRAMESIZE_VGA;
    settings.flashEnabled = false;
}

bool CameraHandler::init() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_LATEST;

    // Configurar según PSRAM disponible
    if (psramFound()) {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        Serial.println("PSRAM encontrado, usando alta resolución");
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        Serial.println("Sin PSRAM, usando resolución media");
    }

    // Inicializar cámara
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Error al inicializar cámara: 0x%x\n", err);
        return false;
    }

    // Configurar pin del flash
    pinMode(FLASH_GPIO_NUM, OUTPUT);
    digitalWrite(FLASH_GPIO_NUM, LOW);

    // Cargar configuración guardada
    loadSettings();

    initialized = true;
    Serial.println("Cámara inicializada correctamente");
    return true;
}

camera_fb_t* CameraHandler::capturePhoto(bool useFlash) {
    if (!initialized) {
        Serial.println("Cámara no inicializada");
        return nullptr;
    }

    // Encender flash si está habilitado y se solicita (para capturas individuales)
    bool flashOn = useFlash && settings.flashEnabled;
    if (flashOn) {
        digitalWrite(FLASH_GPIO_NUM, HIGH);
        delay(150);  // Esperar que el LED alcance su brillo máximo

        // Descartar frames obsoletos del buffer capturados ANTES de encender el flash.
        // El sensor OV2640 usa buffer doble: esp_camera_fb_get() puede devolver un
        // frame antiguo (oscuro) que ya estaba en cola antes de que el flash encendiera.
        // Además, el AEC (Auto Exposure Control) necesita 2-3 frames para recalibrarse
        // con la nueva iluminación del flash.
        for (int i = 0; i < 2; i++) {
            camera_fb_t* dummy = esp_camera_fb_get();
            if (dummy) {
                esp_camera_fb_return(dummy);
            }
            delay(50);
        }
    }

    camera_fb_t* fb = esp_camera_fb_get();

    // Apagar flash después de captura individual
    if (flashOn) {
        digitalWrite(FLASH_GPIO_NUM, LOW);
    }

    if (!fb) {
        Serial.println("Error al capturar foto");
        return nullptr;
    }

    return fb;
}

void CameraHandler::releaseFrame(camera_fb_t* fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

void CameraHandler::setBrightness(int value) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, constrain(value, -2, 2));
        settings.brightness = value;
    }
}

void CameraHandler::setContrast(int value) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_contrast(s, constrain(value, -2, 2));
        settings.contrast = value;
    }
}

void CameraHandler::setSaturation(int value) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_saturation(s, constrain(value, -2, 2));
        settings.saturation = value;
    }
}

void CameraHandler::setSpecialEffect(int effect) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_special_effect(s, constrain(effect, 0, 6));
        settings.specialEffect = effect;
    }
}

void CameraHandler::setWhiteBalance(int mode) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_wb_mode(s, constrain(mode, 0, 4));
        settings.whiteBalance = mode;
    }
}

void CameraHandler::setExposureCtrl(bool enable) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_exposure_ctrl(s, enable ? 1 : 0);
        settings.exposureCtrl = enable ? 1 : 0;
    }
}

void CameraHandler::setAecValue(int value) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_aec_value(s, constrain(value, 0, 1200));
        settings.aecValue = value;
    }
}

void CameraHandler::setGainCtrl(bool enable) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_gain_ctrl(s, enable ? 1 : 0);
        settings.gainCtrl = enable ? 1 : 0;
    }
}

void CameraHandler::setAgcGain(int value) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_agc_gain(s, constrain(value, 0, 30));
        settings.agcGain = value;
    }
}

void CameraHandler::setQuality(int value) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_quality(s, constrain(value, 10, 63));
        settings.quality = value;
    }
}

void CameraHandler::setFrameSize(framesize_t size) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_framesize(s, size);
        settings.frameSize = size;

        // Descartar frames residuales tras el cambio de resolución.
        // Al cambiar resolución el sensor reinicia su pipeline interno y los
        // primeros frames pueden estar mal expuestos o ser de la resolución anterior.
        for (int i = 0; i < 3; i++) {
            camera_fb_t* dummy = esp_camera_fb_get();
            if (dummy) {
                esp_camera_fb_return(dummy);
            }
        }
    }
}

void CameraHandler::setFlash(bool enable) {
    settings.flashEnabled = enable;
    // Flash LED solo se enciende durante capturePhoto() o streaming
    // No se deja encendido permanentemente
}

CameraSettings CameraHandler::getSettings() {
    return settings;
}

void CameraHandler::applySettings(CameraSettings& newSettings) {
    setBrightness(newSettings.brightness);
    setContrast(newSettings.contrast);
    setSaturation(newSettings.saturation);
    setSpecialEffect(newSettings.specialEffect);
    setWhiteBalance(newSettings.whiteBalance);
    setExposureCtrl(newSettings.exposureCtrl);
    setAecValue(newSettings.aecValue);
    setGainCtrl(newSettings.gainCtrl);
    setAgcGain(newSettings.agcGain);
    setQuality(newSettings.quality);
    setFrameSize(newSettings.frameSize);
    settings.flashEnabled = newSettings.flashEnabled;
}

void CameraHandler::saveSettings() {
    prefs.begin("camera", false);
    prefs.putInt("brightness", settings.brightness);
    prefs.putInt("contrast", settings.contrast);
    prefs.putInt("saturation", settings.saturation);
    prefs.putInt("effect", settings.specialEffect);
    prefs.putInt("wb", settings.whiteBalance);
    prefs.putInt("expCtrl", settings.exposureCtrl);
    prefs.putInt("aec", settings.aecValue);
    prefs.putInt("gainCtrl", settings.gainCtrl);
    prefs.putInt("agc", settings.agcGain);
    prefs.putInt("quality", settings.quality);
    prefs.putInt("frameSize", (int)settings.frameSize);
    prefs.putBool("flash", settings.flashEnabled);
    prefs.end();
    Serial.println("Configuración guardada");
}

void CameraHandler::loadSettings() {
    prefs.begin("camera", true);
    settings.brightness = prefs.getInt("brightness", 0);
    settings.contrast = prefs.getInt("contrast", 0);
    settings.saturation = prefs.getInt("saturation", 0);
    settings.specialEffect = prefs.getInt("effect", 0);
    settings.whiteBalance = prefs.getInt("wb", 0);
    settings.exposureCtrl = prefs.getInt("expCtrl", 1);
    settings.aecValue = prefs.getInt("aec", 300);
    settings.gainCtrl = prefs.getInt("gainCtrl", 1);
    settings.agcGain = prefs.getInt("agc", 0);
    settings.quality = prefs.getInt("quality", 12);
    settings.frameSize = (framesize_t)prefs.getInt("frameSize", FRAMESIZE_VGA);
    settings.flashEnabled = prefs.getBool("flash", false);
    prefs.end();

    // Aplicar configuración cargada
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, settings.brightness);
        s->set_contrast(s, settings.contrast);
        s->set_saturation(s, settings.saturation);
        s->set_special_effect(s, settings.specialEffect);
        s->set_wb_mode(s, settings.whiteBalance);
        s->set_exposure_ctrl(s, settings.exposureCtrl);
        s->set_aec_value(s, settings.aecValue);
        s->set_gain_ctrl(s, settings.gainCtrl);
        s->set_agc_gain(s, settings.agcGain);
        s->set_quality(s, settings.quality);
        s->set_framesize(s, settings.frameSize);
    }

    Serial.println("Configuración cargada");
}
