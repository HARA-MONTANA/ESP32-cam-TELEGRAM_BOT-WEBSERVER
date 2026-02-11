#include "web_server.h"
#include "camera_handler.h"
#include "sd_handler.h"
#include "config.h"
#include "esp_camera.h"
#include <time.h>

CameraWebServer webServer(WEB_SERVER_PORT);

CameraWebServer::CameraWebServer(int port) : server(port) {}

void CameraWebServer::init() {
    // Rutas del servidor
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/stream", HTTP_GET, [this]() { handleStream(); });
    server.on("/capture", HTTP_GET, [this]() { handleCapture(); });
    server.on("/settings", HTTP_GET, [this]() { handleGetSettings(); });
    server.on("/settings", HTTP_POST, [this]() { handleUpdateSettings(); });
    server.on("/status", HTTP_GET, [this]() { handleStatus(); });
    server.on("/web-capture", HTTP_GET, [this]() { handleWebCapture(); });
    server.on("/folders", HTTP_GET, [this]() { handleListFolders(); });
    server.on("/photos", HTTP_GET, [this]() { handleListPhotos(); });
    server.on("/photo", HTTP_GET, [this]() { handleViewPhoto(); });
    server.on("/delete-photo", HTTP_POST, [this]() { handleDeletePhoto(); });
    server.onNotFound([this]() { handleNotFound(); });

    // Crear carpeta para fotos capturadas desde la web
    if (sdCard.isInitialized()) {
        SD_MMC.mkdir("/" WEB_PHOTOS_FOLDER);
    }

    server.begin();
    Serial.println("Servidor web iniciado en puerto " + String(WEB_SERVER_PORT));
}

void CameraWebServer::handleClient() {
    server.handleClient();
}

void CameraWebServer::handleRoot() {
    server.send(200, "text/html", generateDashboardHTML());
}

void CameraWebServer::handleCapture() {
    camera_fb_t* fb = camera.capturePhoto();
    if (!fb) {
        server.send(500, "text/plain", "Error al capturar imagen");
        return;
    }

    server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
    server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    camera.releaseFrame(fb);
}

void CameraWebServer::handleStream() {
    WiFiClient client = server.client();

    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    client.print(response);

    // Encender flash al inicio del stream si está habilitado
    CameraSettings settings = camera.getSettings();
    if (settings.flashEnabled) {
        digitalWrite(FLASH_GPIO_NUM, HIGH);
    }

    while (client.connected()) {
        // Capturar sin activar flash (ya está encendido si corresponde)
        camera_fb_t* fb = camera.capturePhoto(false);
        if (!fb) {
            Serial.println("Error en stream: captura fallida");
            break;
        }

        String header = "--frame\r\n";
        header += "Content-Type: image/jpeg\r\n";
        header += "Content-Length: " + String(fb->len) + "\r\n\r\n";

        // Verificar que los datos se envían correctamente
        // Si write retorna 0, el cliente se desconectó
        if (client.print(header) == 0) {
            camera.releaseFrame(fb);
            break;
        }
        if (client.write(fb->buf, fb->len) == 0) {
            camera.releaseFrame(fb);
            break;
        }
        if (client.print("\r\n") == 0) {
            camera.releaseFrame(fb);
            break;
        }

        camera.releaseFrame(fb);
        delay(30);  // ~30 FPS
    }

    // Siempre apagar flash LED al terminar el stream
    digitalWrite(FLASH_GPIO_NUM, LOW);
    Serial.println("Stream finalizado");
}

void CameraWebServer::handleWebCapture() {
    camera_fb_t* fb = camera.capturePhoto();
    if (!fb) {
        server.send(500, "text/plain", "Error al capturar imagen");
        return;
    }

    // Guardar en SD si está disponible
    if (sdCard.isInitialized()) {
        if (!SD_MMC.exists("/" WEB_PHOTOS_FOLDER)) {
            SD_MMC.mkdir("/" WEB_PHOTOS_FOLDER);
        }

        struct tm timeinfo;
        String filename;
        if (getLocalTime(&timeinfo)) {
            char buf[80];
            snprintf(buf, sizeof(buf), "/%s/web_%04d-%02d-%02d_%02d-%02d-%02d.jpg",
                     WEB_PHOTOS_FOLDER,
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            filename = String(buf);
        } else {
            filename = "/" + String(WEB_PHOTOS_FOLDER) + "/web_" + String(millis()) + ".jpg";
        }

        sdCard.savePhoto(fb->buf, fb->len, filename);
        Serial.printf("Foto web guardada: %s\n", filename.c_str());

        // Enviar nombre del archivo para que el frontend pueda referenciarlo
        String justName = filename.substring(filename.lastIndexOf('/') + 1);
        server.sendHeader("X-Photo-Name", justName);
    }

    server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
    server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    camera.releaseFrame(fb);
}

void CameraWebServer::handleListFolders() {
    if (!sdCard.isInitialized()) {
        server.send(200, "application/json", "[]");
        return;
    }

    File root = SD_MMC.open("/");
    if (!root || !root.isDirectory()) {
        server.send(200, "application/json", "[]");
        return;
    }

    String json = "[";
    bool first = true;
    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String name = String(entry.name());
            if (name.startsWith("/")) name = name.substring(1);
            if (!name.isEmpty() && !name.startsWith(".") && name != "System Volume Information") {
                // Count photos in folder
                int count = 0;
                File dir = SD_MMC.open("/" + name);
                if (dir && dir.isDirectory()) {
                    File f = dir.openNextFile();
                    while (f) {
                        if (!f.isDirectory()) {
                            String fname = String(f.name());
                            if (fname.endsWith(".jpg") || fname.endsWith(".JPG")) count++;
                        }
                        f = dir.openNextFile();
                    }
                    dir.close();
                }
                if (!first) json += ",";
                json += "{\"name\":\"" + name + "\",\"count\":" + String(count) + "}";
                first = false;
            }
        }
        entry = root.openNextFile();
    }
    root.close();
    json += "]";

    server.send(200, "application/json", json);
}

void CameraWebServer::handleListPhotos() {
    if (!sdCard.isInitialized()) {
        server.send(200, "application/json", "[]");
        return;
    }

    String folder = WEB_PHOTOS_FOLDER;
    if (server.hasArg("folder")) {
        folder = server.arg("folder");
        if (folder.indexOf("..") >= 0) {
            server.send(400, "application/json", "[]");
            return;
        }
    }

    String folderPath = "/" + folder;
    if (!SD_MMC.exists(folderPath)) {
        server.send(200, "application/json", "[]");
        return;
    }

    File dir = SD_MMC.open(folderPath);
    if (!dir || !dir.isDirectory()) {
        server.send(200, "application/json", "[]");
        return;
    }

    String json = "[";
    bool first = true;
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = String(file.name());
            if (name.endsWith(".jpg") || name.endsWith(".JPG")) {
                if (!first) json += ",";
                json += "{\"name\":\"" + name + "\",\"size\":" + String(file.size()) + "}";
                first = false;
            }
        }
        file = dir.openNextFile();
    }
    dir.close();
    json += "]";

    server.send(200, "application/json", json);
}

void CameraWebServer::handleViewPhoto() {
    if (!server.hasArg("name")) {
        server.send(400, "text/plain", "Falta parametro name");
        return;
    }

    String name = server.arg("name");
    if (name.indexOf("..") >= 0) {
        server.send(400, "text/plain", "Nombre invalido");
        return;
    }

    String folder = WEB_PHOTOS_FOLDER;
    if (server.hasArg("folder")) {
        folder = server.arg("folder");
        if (folder.indexOf("..") >= 0) {
            server.send(400, "text/plain", "Nombre invalido");
            return;
        }
    }

    String filename = "/" + folder + "/" + name;
    size_t size = 0;
    uint8_t* data = sdCard.readPhoto(filename, size);
    if (!data) {
        server.send(404, "text/plain", "Foto no encontrada");
        return;
    }

    if (server.hasArg("dl")) {
        server.sendHeader("Content-Disposition", "attachment; filename=" + name);
    } else {
        server.sendHeader("Content-Disposition", "inline; filename=" + name);
    }
    server.send_P(200, "image/jpeg", (const char*)data, size);
    sdCard.freePhotoBuffer(data);
}

void CameraWebServer::handleDeletePhoto() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Sin datos\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error || !doc.containsKey("name")) {
        server.send(400, "application/json", "{\"error\":\"JSON invalido\"}");
        return;
    }

    String name = doc["name"].as<String>();
    if (name.indexOf("..") >= 0) {
        server.send(400, "application/json", "{\"error\":\"Nombre invalido\"}");
        return;
    }

    String folder = WEB_PHOTOS_FOLDER;
    if (doc.containsKey("folder")) {
        folder = doc["folder"].as<String>();
        if (folder.indexOf("..") >= 0) {
            server.send(400, "application/json", "{\"error\":\"Nombre invalido\"}");
            return;
        }
    }

    String filename = "/" + folder + "/" + name;
    if (sdCard.deletePhoto(filename)) {
        server.send(200, "application/json", "{\"success\":true}");
        Serial.printf("Foto eliminada: %s\n", filename.c_str());
    } else {
        server.send(500, "application/json", "{\"error\":\"No se pudo eliminar\"}");
    }
}

void CameraWebServer::handleGetSettings() {
    CameraSettings settings = camera.getSettings();

    StaticJsonDocument<512> doc;
    doc["brightness"] = settings.brightness;
    doc["contrast"] = settings.contrast;
    doc["saturation"] = settings.saturation;
    doc["specialEffect"] = settings.specialEffect;
    doc["whiteBalance"] = settings.whiteBalance;
    doc["exposureCtrl"] = settings.exposureCtrl;
    doc["aecValue"] = settings.aecValue;
    doc["gainCtrl"] = settings.gainCtrl;
    doc["agcGain"] = settings.agcGain;
    doc["quality"] = settings.quality;
    doc["frameSize"] = (int)settings.frameSize;
    doc["flash"] = settings.flashEnabled;

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void CameraWebServer::handleUpdateSettings() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error) {
            server.send(400, "application/json", "{\"error\":\"JSON inválido\"}");
            return;
        }

        if (doc.containsKey("brightness")) camera.setBrightness(doc["brightness"]);
        if (doc.containsKey("contrast")) camera.setContrast(doc["contrast"]);
        if (doc.containsKey("saturation")) camera.setSaturation(doc["saturation"]);
        if (doc.containsKey("specialEffect")) camera.setSpecialEffect(doc["specialEffect"]);
        if (doc.containsKey("whiteBalance")) camera.setWhiteBalance(doc["whiteBalance"]);
        if (doc.containsKey("exposureCtrl")) camera.setExposureCtrl(doc["exposureCtrl"]);
        if (doc.containsKey("aecValue")) camera.setAecValue(doc["aecValue"]);
        if (doc.containsKey("gainCtrl")) camera.setGainCtrl(doc["gainCtrl"]);
        if (doc.containsKey("agcGain")) camera.setAgcGain(doc["agcGain"]);
        if (doc.containsKey("quality")) camera.setQuality(doc["quality"]);
        if (doc.containsKey("frameSize")) camera.setFrameSize((framesize_t)doc["frameSize"].as<int>());
        if (doc.containsKey("flash")) camera.setFlash(doc["flash"]);

        if (doc.containsKey("save") && doc["save"].as<bool>()) {
            camera.saveSettings();
        }

        server.send(200, "application/json", "{\"success\":true}");
    } else {
        server.send(400, "application/json", "{\"error\":\"Sin datos\"}");
    }
}

void CameraWebServer::handleStatus() {
    StaticJsonDocument<256> doc;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["psramSize"] = ESP.getPsramSize();
    doc["freePsram"] = ESP.getFreePsram();
    doc["sdInitialized"] = sdCard.isInitialized();

    if (sdCard.isInitialized()) {
        doc["sdTotal"] = sdCard.getTotalSpace() / (1024 * 1024);
        doc["sdUsed"] = sdCard.getUsedSpace() / (1024 * 1024);
        doc["sdFree"] = sdCard.getFreeSpace() / (1024 * 1024);
    }

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void CameraWebServer::handleNotFound() {
    server.send(404, "text/plain", "No encontrado");
}

String CameraWebServer::generateDashboardHTML() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-CAM Dashboard</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #0a0a1a 0%, #0d0d2b 50%, #0a0a1a 100%);
            min-height: 100vh;
            color: #e0e0e0;
            padding: 20px;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        h1 {
            text-align: center;
            margin-bottom: 30px;
            color: #0ff;
            text-shadow: 0 0 10px #0ff, 0 0 30px #0ff, 0 0 60px rgba(0, 255, 255, 0.3);
            letter-spacing: 2px;
        }
        .grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
        }
        @media (max-width: 900px) {
            .grid { grid-template-columns: 1fr; }
        }
        .card {
            background: rgba(10, 10, 30, 0.8);
            backdrop-filter: blur(10px);
            border-radius: 15px;
            padding: 20px;
            border: 1px solid rgba(0, 255, 255, 0.15);
            box-shadow: 0 0 15px rgba(0, 255, 255, 0.05), inset 0 0 15px rgba(0, 255, 255, 0.02);
        }
        .card:hover {
            border-color: rgba(0, 255, 255, 0.3);
            box-shadow: 0 0 20px rgba(0, 255, 255, 0.1), inset 0 0 20px rgba(0, 255, 255, 0.03);
        }
        .card h2 {
            color: #e0ff00;
            margin-bottom: 20px;
            font-size: 1.2em;
            border-bottom: 1px solid rgba(224, 255, 0, 0.25);
            padding-bottom: 10px;
            text-shadow: 0 0 8px rgba(224, 255, 0, 0.4);
            letter-spacing: 1px;
        }
        .stream-container {
            position: relative;
            width: 100%;
            background: #000;
            border-radius: 10px;
            overflow: hidden;
            margin-bottom: 15px;
            border: 1px solid rgba(255, 0, 255, 0.2);
            box-shadow: 0 0 15px rgba(255, 0, 255, 0.1);
        }
        .stream-container img {
            width: 100%;
            display: block;
        }
        .btn-group {
            display: flex;
            gap: 10px;
            flex-wrap: wrap;
        }
        .btn {
            flex: 1;
            min-width: 100px;
            padding: 12px 20px;
            border: none;
            border-radius: 8px;
            font-size: 14px;
            cursor: pointer;
            transition: all 0.3s ease;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        .btn-primary {
            background: linear-gradient(135deg, #ff00ff, #aa00aa);
            color: #fff;
            box-shadow: 0 0 10px rgba(255, 0, 255, 0.3);
        }
        .btn-primary:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 25px rgba(255, 0, 255, 0.5);
        }
        .btn-success {
            background: linear-gradient(135deg, #00f0ff, #0099aa);
            color: #000;
            box-shadow: 0 0 10px rgba(0, 240, 255, 0.3);
        }
        .btn-success:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 25px rgba(0, 240, 255, 0.5);
        }
        .btn-warning {
            background: linear-gradient(135deg, #e0ff00, #aacc00);
            color: #000;
            box-shadow: 0 0 10px rgba(224, 255, 0, 0.3);
        }
        .btn-warning:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 25px rgba(224, 255, 0, 0.5);
        }
        .control-group {
            margin-bottom: 20px;
        }
        .control-group label {
            display: block;
            margin-bottom: 8px;
            color: #0ff;
            font-size: 0.9em;
            text-shadow: 0 0 5px rgba(0, 255, 255, 0.3);
        }
        .slider-container {
            display: flex;
            align-items: center;
            gap: 15px;
        }
        input[type="range"] {
            flex: 1;
            -webkit-appearance: none;
            height: 8px;
            background: rgba(0, 255, 255, 0.1);
            border-radius: 4px;
            outline: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 20px;
            height: 20px;
            background: #e0ff00;
            border-radius: 50%;
            cursor: pointer;
            box-shadow: 0 0 10px rgba(224, 255, 0, 0.6), 0 0 20px rgba(224, 255, 0, 0.3);
        }
        .slider-value {
            min-width: 40px;
            text-align: center;
            font-weight: bold;
            color: #e0ff00;
            text-shadow: 0 0 5px rgba(224, 255, 0, 0.5);
        }
        select {
            width: 100%;
            padding: 10px;
            border-radius: 8px;
            border: 1px solid rgba(0, 255, 255, 0.2);
            background: rgba(10, 10, 30, 0.8);
            color: #e0e0e0;
            font-size: 14px;
            cursor: pointer;
        }
        select:focus {
            border-color: rgba(0, 255, 255, 0.5);
            box-shadow: 0 0 10px rgba(0, 255, 255, 0.2);
            outline: none;
        }
        select option { background: #0a0a1a; color: #e0e0e0; }
        .switch-container {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 10px 0;
        }
        .switch-container label:first-child {
            color: #0ff;
            text-shadow: 0 0 5px rgba(0, 255, 255, 0.3);
        }
        .switch {
            position: relative;
            width: 50px;
            height: 26px;
        }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider-toggle {
            position: absolute;
            cursor: pointer;
            top: 0; left: 0; right: 0; bottom: 0;
            background-color: rgba(255, 255, 255, 0.1);
            transition: 0.4s;
            border-radius: 26px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        .slider-toggle:before {
            position: absolute;
            content: "";
            height: 20px;
            width: 20px;
            left: 3px;
            bottom: 2px;
            background-color: #555;
            transition: 0.4s;
            border-radius: 50%;
        }
        input:checked + .slider-toggle {
            background: linear-gradient(135deg, #ff00ff, #aa00aa);
            border-color: rgba(255, 0, 255, 0.4);
            box-shadow: 0 0 10px rgba(255, 0, 255, 0.3);
        }
        input:checked + .slider-toggle:before {
            transform: translateX(24px);
            background-color: #fff;
        }
        .status-bar {
            display: flex;
            justify-content: space-around;
            flex-wrap: wrap;
            gap: 10px;
            margin-top: 20px;
            padding: 15px;
            background: rgba(0, 0, 0, 0.4);
            border-radius: 10px;
            border: 1px solid rgba(0, 255, 255, 0.1);
        }
        .status-item {
            text-align: center;
        }
        .status-item .value {
            font-size: 1.5em;
            font-weight: bold;
            color: #0ff;
            text-shadow: 0 0 8px rgba(0, 255, 255, 0.5);
        }
        .status-item .label {
            font-size: 0.8em;
            color: #888;
        }
        .sd-bar-container {
            width: 100%;
            min-width: 120px;
            height: 8px;
            background: rgba(255,255,255,0.1);
            border-radius: 4px;
            margin-top: 6px;
            overflow: hidden;
        }
        .sd-bar-fill {
            height: 100%;
            border-radius: 4px;
            transition: width 0.5s ease, background 0.5s ease;
        }
        .folder-tab {
            padding: 8px 16px;
            border: 1px solid rgba(0, 255, 255, 0.2);
            border-radius: 8px;
            background: rgba(10, 10, 30, 0.6);
            color: #888;
            cursor: pointer;
            font-size: 0.85em;
            font-weight: 600;
            transition: all 0.3s ease;
        }
        .folder-tab:hover {
            border-color: rgba(0, 255, 255, 0.4);
            color: #0ff;
        }
        .folder-tab.active {
            background: linear-gradient(135deg, rgba(0, 255, 255, 0.15), rgba(0, 255, 255, 0.05));
            border-color: #0ff;
            color: #0ff;
            box-shadow: 0 0 10px rgba(0, 255, 255, 0.2);
            text-shadow: 0 0 5px rgba(0, 255, 255, 0.4);
        }
        .toast {
            position: fixed;
            bottom: 20px;
            right: 20px;
            padding: 15px 25px;
            background: linear-gradient(135deg, #ff00ff, #aa00aa);
            color: #fff;
            border-radius: 10px;
            font-weight: bold;
            transform: translateY(100px);
            opacity: 0;
            transition: all 0.3s ease;
            z-index: 1000;
            box-shadow: 0 0 20px rgba(255, 0, 255, 0.4);
            text-shadow: 0 0 5px rgba(255, 255, 255, 0.5);
        }
        .toast.show {
            transform: translateY(0);
            opacity: 1;
        }
        .photo-viewer {
            display: none;
            margin-bottom: 15px;
            border: 1px solid rgba(255, 0, 255, 0.25);
            border-radius: 10px;
            overflow: hidden;
            background: #000;
            box-shadow: 0 0 15px rgba(255, 0, 255, 0.1);
        }
        .photo-viewer.active { display: block; }
        .photo-viewer img {
            width: 100%;
            display: block;
            max-height: 400px;
            object-fit: contain;
            background: #111;
        }
        .viewer-bar {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 8px 12px;
            background: rgba(10, 10, 30, 0.95);
            border-top: 1px solid rgba(0, 255, 255, 0.1);
        }
        .viewer-bar button {
            padding: 6px 14px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-size: 0.8em;
            font-weight: 600;
            transition: all 0.2s ease;
        }
        .viewer-nav {
            background: linear-gradient(135deg, #ff00ff, #aa00aa);
            color: #fff;
            box-shadow: 0 0 8px rgba(255, 0, 255, 0.3);
        }
        .viewer-nav:hover { box-shadow: 0 0 15px rgba(255, 0, 255, 0.5); }
        .viewer-nav:disabled { opacity: 0.3; cursor: default; box-shadow: none; }
        .viewer-close {
            background: linear-gradient(135deg, #ff0055, #aa0033);
            color: #fff;
        }
        .viewer-info {
            text-align: center;
            flex: 1;
            padding: 0 10px;
        }
        .viewer-info .name {
            color: #0ff;
            font-size: 0.85em;
            display: block;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        .viewer-info .counter {
            color: #888;
            font-size: 0.75em;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32-CAM Dashboard</h1>

        <div class="grid">
            <div class="card">
                <h2>Vista en Vivo</h2>
                <div class="stream-container">
                    <img id="stream" src="/capture" alt="Stream">
                </div>
                <div class="btn-group">
                    <button class="btn btn-primary" onclick="toggleStream()">
                        <span id="streamBtn">Iniciar Stream</span>
                    </button>
                    <button class="btn btn-success" onclick="capturePhoto()">Capturar Foto</button>
                </div>
            </div>

            <div class="card">
                <h2>Ajustes de Imagen</h2>

                <div class="control-group">
                    <label>Brillo</label>
                    <div class="slider-container">
                        <input type="range" id="brightness" min="-2" max="2" value="0" onchange="updateSetting('brightness', this.value)">
                        <span class="slider-value" id="brightnessVal">0</span>
                    </div>
                </div>

                <div class="control-group">
                    <label>Contraste</label>
                    <div class="slider-container">
                        <input type="range" id="contrast" min="-2" max="2" value="0" onchange="updateSetting('contrast', this.value)">
                        <span class="slider-value" id="contrastVal">0</span>
                    </div>
                </div>

                <div class="control-group">
                    <label>Saturacion</label>
                    <div class="slider-container">
                        <input type="range" id="saturation" min="-2" max="2" value="0" onchange="updateSetting('saturation', this.value)">
                        <span class="slider-value" id="saturationVal">0</span>
                    </div>
                </div>

                <div class="control-group">
                    <label>Calidad JPEG (menor = mejor)</label>
                    <div class="slider-container">
                        <input type="range" id="quality" min="10" max="63" value="12" onchange="updateSetting('quality', this.value)">
                        <span class="slider-value" id="qualityVal">12</span>
                    </div>
                </div>

                <div class="control-group">
                    <label>Resolucion</label>
                    <select id="frameSize" onchange="updateSetting('frameSize', parseInt(this.value))">
                        <option value="0">96x96</option>
                        <option value="1">160x120</option>
                        <option value="2">176x144</option>
                        <option value="3">240x176</option>
                        <option value="4">240x240</option>
                        <option value="5">320x240 (QVGA)</option>
                        <option value="6">400x296</option>
                        <option value="7">480x320</option>
                        <option value="8" selected>640x480 (VGA)</option>
                        <option value="9">800x600 (SVGA)</option>
                        <option value="10">1024x768 (XGA)</option>
                        <option value="11">1280x720 (HD)</option>
                        <option value="12">1280x1024 (SXGA)</option>
                        <option value="13">1600x1200 (UXGA)</option>
                    </select>
                </div>
            </div>

            <div class="card">
                <h2>Efectos y Balance</h2>

                <div class="control-group">
                    <label>Efecto Especial</label>
                    <select id="specialEffect" onchange="updateSetting('specialEffect', parseInt(this.value))">
                        <option value="0">Sin Efecto</option>
                        <option value="1">Negativo</option>
                        <option value="2">Escala de Grises</option>
                        <option value="3">Tono Rojo</option>
                        <option value="4">Tono Verde</option>
                        <option value="5">Tono Azul</option>
                        <option value="6">Sepia</option>
                    </select>
                </div>

                <div class="control-group">
                    <label>Balance de Blancos</label>
                    <select id="whiteBalance" onchange="updateSetting('whiteBalance', parseInt(this.value))">
                        <option value="0">Automatico</option>
                        <option value="1">Soleado</option>
                        <option value="2">Nublado</option>
                        <option value="3">Oficina</option>
                        <option value="4">Hogar</option>
                    </select>
                </div>

                <div class="switch-container">
                    <label>Flash LED</label>
                    <label class="switch">
                        <input type="checkbox" id="flash" onchange="updateSetting('flash', this.checked)">
                        <span class="slider-toggle"></span>
                    </label>
                </div>

                <div class="switch-container">
                    <label>Exposicion Automatica</label>
                    <label class="switch">
                        <input type="checkbox" id="exposureCtrl" checked onchange="updateSetting('exposureCtrl', this.checked)">
                        <span class="slider-toggle"></span>
                    </label>
                </div>

                <div class="switch-container">
                    <label>Ganancia Automatica</label>
                    <label class="switch">
                        <input type="checkbox" id="gainCtrl" checked onchange="updateSetting('gainCtrl', this.checked)">
                        <span class="slider-toggle"></span>
                    </label>
                </div>

                <div class="btn-group" style="margin-top: 20px;">
                    <button class="btn btn-warning" onclick="saveSettings()">Guardar Configuracion</button>
                </div>
            </div>

            <div class="card">
                <h2>Estado del Sistema</h2>
                <div class="status-bar">
                    <div class="status-item">
                        <div class="value" id="heapValue">--</div>
                        <div class="label">Heap Libre (KB)</div>
                    </div>
                    <div class="status-item">
                        <div class="value" id="psramValue">--</div>
                        <div class="label">PSRAM Libre (KB)</div>
                    </div>
                    <div class="status-item">
                        <div class="value" id="sdValue">--</div>
                        <div class="label">SD Card</div>
                        <div class="sd-bar-container" id="sdBarContainer" style="display:none;">
                            <div class="sd-bar-fill" id="sdBarFill"></div>
                        </div>
                    </div>
                </div>
                <div class="btn-group" style="margin-top: 20px;">
                    <button class="btn btn-primary" onclick="loadStatus()">Actualizar Estado</button>
                </div>
            </div>

            <div class="card" style="grid-column: 1 / -1;">
                <h2>Galeria de Fotos</h2>
                <div id="folderTabs" style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:15px;">
                    <p style="color:#888;font-size:0.85em;">Cargando carpetas...</p>
                </div>
                <div id="photoViewer" class="photo-viewer">
                    <img id="viewerImg" src="" alt="Vista previa">
                    <div class="viewer-bar">
                        <button class="viewer-nav" id="prevBtn" onclick="prevPhoto()">&#9664; Anterior</button>
                        <div class="viewer-info">
                            <span class="name" id="viewerName"></span>
                            <span id="viewerRaw" style="color:#555;font-size:0.7em;"></span>
                            <span class="counter" id="viewerCounter"></span>
                        </div>
                        <button class="viewer-nav" id="nextBtn" onclick="nextPhoto()">Siguiente &#9654;</button>
                    </div>
                    <div class="viewer-bar" style="border-top:none;justify-content:center;gap:8px;padding-top:0;">
                        <button onclick="downloadPhoto(photoList[currentPhotoIndex].name)" style="padding:6px 14px;background:linear-gradient(135deg,#e0ff00,#aacc00);color:#000;border:none;border-radius:6px;cursor:pointer;font-size:0.8em;font-weight:600;">Descargar</button>
                        <button onclick="window.open('/photo?folder='+encodeURIComponent(currentFolder)+'&name='+encodeURIComponent(photoList[currentPhotoIndex].name),'_blank')" style="padding:6px 14px;background:linear-gradient(135deg,#00f0ff,#0099aa);color:#000;border:none;border-radius:6px;cursor:pointer;font-size:0.8em;font-weight:600;">Abrir en Pestana</button>
                        <button class="viewer-close" onclick="closeViewer()">Cerrar</button>
                    </div>
                </div>
                <div id="photoGallery" style="max-height: 350px; overflow-y: auto; padding-right: 5px;">
                    <p style="color: #888; text-align: center;">Cargando...</p>
                </div>
                <div class="btn-group" style="margin-top: 15px;">
                    <button class="btn btn-success" onclick="loadPhotos()">Actualizar Lista</button>
                </div>
            </div>
        </div>
    </div>

    <div id="toast" class="toast"></div>

    <script>
        let streaming = false;
        let photoList = [];
        let currentPhotoIndex = -1;
        let currentFolder = 'fotos_web';
        let folderList = [];

        function showToast(message) {
            const toast = document.getElementById('toast');
            toast.textContent = message;
            toast.classList.add('show');
            setTimeout(() => toast.classList.remove('show'), 3000);
        }

        function toggleStream() {
            const img = document.getElementById('stream');
            const btn = document.getElementById('streamBtn');
            streaming = !streaming;

            if (streaming) {
                img.src = '/stream?' + Date.now();
                btn.textContent = 'Detener Stream';
            } else {
                img.src = '';
                const parent = img.parentNode;
                const newImg = document.createElement('img');
                newImg.id = 'stream';
                newImg.alt = 'Stream';
                parent.replaceChild(newImg, img);
                btn.textContent = 'Iniciar Stream';
                setTimeout(() => {
                    const currentImg = document.getElementById('stream');
                    if (currentImg && !streaming) {
                        currentImg.src = '/capture?' + Date.now();
                    }
                }, 500);
            }
        }

        async function doCapture() {
            try {
                const response = await fetch('/web-capture?' + Date.now());
                if (!response.ok) {
                    showToast('Error al capturar');
                    return;
                }
                const photoName = response.headers.get('X-Photo-Name');
                const img = document.getElementById('stream');
                if (photoName) {
                    img.src = '/photo?folder=fotos_web&name=' + encodeURIComponent(photoName);
                    showToast('Foto guardada: ' + photoName);
                    if (currentFolder === 'fotos_web') {
                        await loadPhotos();
                        if (photoList.length > 0) showViewer(0);
                    } else {
                        selectFolder('fotos_web');
                    }
                } else {
                    const blob = await response.blob();
                    img.src = URL.createObjectURL(blob);
                    showToast('Foto capturada (sin SD)');
                }
            } catch (error) {
                showToast('Error al capturar');
            }
        }

        function capturePhoto() {
            const btn = document.getElementById('streamBtn');

            if (streaming) {
                const img = document.getElementById('stream');
                streaming = false;
                img.src = '';
                const parent = img.parentNode;
                const newImg = document.createElement('img');
                newImg.id = 'stream';
                newImg.alt = 'Stream';
                parent.replaceChild(newImg, img);
                btn.textContent = 'Iniciar Stream';
                setTimeout(doCapture, 500);
            } else {
                doCapture();
            }
        }

        async function updateSetting(name, value) {
            const slider = document.getElementById(name + 'Val');
            if (slider) slider.textContent = value;

            if (name === 'frameSize' && streaming) {
                const img = document.getElementById('stream');
                const btn = document.getElementById('streamBtn');
                streaming = false;
                img.src = '';
                const parent = img.parentNode;
                const newImg = document.createElement('img');
                newImg.id = 'stream';
                newImg.alt = 'Stream';
                parent.replaceChild(newImg, img);

                await new Promise(r => setTimeout(r, 500));
                try {
                    await fetch('/settings', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ [name]: value })
                    });
                } catch (error) {
                    showToast('Error al actualizar');
                    btn.textContent = 'Iniciar Stream';
                    return;
                }

                streaming = true;
                btn.textContent = 'Detener Stream';
                document.getElementById('stream').src = '/stream?' + Date.now();
                return;
            }

            try {
                await fetch('/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ [name]: value })
                });
            } catch (error) {
                showToast('Error al actualizar');
            }
        }

        async function saveSettings() {
            try {
                const response = await fetch('/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ save: true })
                });
                if (response.ok) {
                    showToast('Configuracion guardada');
                }
            } catch (error) {
                showToast('Error al guardar');
            }
        }

        async function loadSettings() {
            try {
                const response = await fetch('/settings');
                const settings = await response.json();

                document.getElementById('brightness').value = settings.brightness;
                document.getElementById('brightnessVal').textContent = settings.brightness;
                document.getElementById('contrast').value = settings.contrast;
                document.getElementById('contrastVal').textContent = settings.contrast;
                document.getElementById('saturation').value = settings.saturation;
                document.getElementById('saturationVal').textContent = settings.saturation;
                document.getElementById('quality').value = settings.quality;
                document.getElementById('qualityVal').textContent = settings.quality;
                document.getElementById('frameSize').value = settings.frameSize;
                document.getElementById('specialEffect').value = settings.specialEffect;
                document.getElementById('whiteBalance').value = settings.whiteBalance;
                document.getElementById('flash').checked = settings.flash;
                document.getElementById('exposureCtrl').checked = settings.exposureCtrl;
                document.getElementById('gainCtrl').checked = settings.gainCtrl;
            } catch (error) {
                console.error('Error loading settings:', error);
            }
        }

        async function loadStatus() {
            try {
                const response = await fetch('/status');
                const status = await response.json();

                document.getElementById('heapValue').textContent = Math.round(status.freeHeap / 1024);
                document.getElementById('psramValue').textContent = Math.round(status.freePsram / 1024);

                if (status.sdInitialized && status.sdTotal > 0) {
                    var freeGB = (status.sdFree / 1024).toFixed(1);
                    var totalGB = (status.sdTotal / 1024).toFixed(1);
                    document.getElementById('sdValue').textContent = freeGB + '/' + totalGB + ' GB Libres';
                    var usedPct = ((status.sdUsed / status.sdTotal) * 100).toFixed(1);
                    var bar = document.getElementById('sdBarFill');
                    bar.style.width = usedPct + '%';
                    if (usedPct > 90) bar.style.background = 'linear-gradient(90deg,#ff00ff,#aa00aa)';
                    else if (usedPct > 70) bar.style.background = 'linear-gradient(90deg,#e0ff00,#aacc00)';
                    else bar.style.background = 'linear-gradient(90deg,#00f0ff,#0099aa)';
                    document.getElementById('sdBarContainer').style.display = 'block';
                } else {
                    document.getElementById('sdValue').textContent = '--';
                    document.getElementById('sdBarContainer').style.display = 'none';
                }
            } catch (error) {
                console.error('Error loading status:', error);
            }
        }

        function getFolderDisplayName(name) {
            if (name === 'fotos_diarias') return 'Diarias';
            if (name === 'fotos_telegram') return 'Telegram';
            if (name === 'fotos_web') return 'Web';
            return name;
        }

        async function loadFolders() {
            try {
                const response = await fetch('/folders');
                const folders = await response.json();
                folderList = folders;
                const container = document.getElementById('folderTabs');

                if (folders.length === 0) {
                    container.innerHTML = '<p style="color:#888;font-size:0.85em;">No hay carpetas</p>';
                    return;
                }

                let html = '';
                folders.forEach(f => {
                    const isActive = f.name === currentFolder ? ' active' : '';
                    html += '<button class="folder-tab' + isActive + '" onclick="selectFolder(\'' + f.name + '\')">';
                    html += getFolderDisplayName(f.name) + ' (' + f.count + ')';
                    html += '</button>';
                });
                container.innerHTML = html;
            } catch (error) {
                console.error('Error loading folders:', error);
            }
        }

        function selectFolder(folder) {
            currentFolder = folder;
            closeViewer();
            // Update tab active state
            document.querySelectorAll('.folder-tab').forEach(tab => {
                tab.classList.remove('active');
                if (tab.textContent.startsWith(getFolderDisplayName(folder))) {
                    tab.classList.add('active');
                }
            });
            loadPhotos();
        }

        function formatPhotoDate(name) {
            var d = name;
            if (d.startsWith('web_')) d = d.substring(4);
            if (d.length >= 16) {
                var s = d.substring(8,10)+'/'+d.substring(5,7)+'/'+d.substring(0,4)+' '+d.substring(11,13)+':'+d.substring(14,16);
                if (d.length >= 19 && d.charAt(16) === '-') s += ':'+d.substring(17,19);
                return s;
            }
            return name;
        }

        async function loadPhotos() {
            try {
                const response = await fetch('/photos?folder=' + encodeURIComponent(currentFolder));
                const photos = await response.json();
                const gallery = document.getElementById('photoGallery');

                if (photos.length === 0) {
                    photoList = [];
                    gallery.innerHTML = '<p style="color:#888;text-align:center;">No hay fotos en ' + getFolderDisplayName(currentFolder) + '</p>';
                    return;
                }

                photos.sort((a, b) => b.name.localeCompare(a.name));
                photoList = photos;
                let html = '';
                photos.forEach(photo => {
                    const sizeKB = Math.round(photo.size / 1024);
                    html += '<div style="display:flex;align-items:center;justify-content:space-between;padding:8px 5px;border-bottom:1px solid rgba(0,255,255,0.1);">';
                    html += '<div style="flex:1;overflow:hidden;min-width:0;">';
                    html += '<div style="color:#0ff;font-size:0.85em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;">' + formatPhotoDate(photo.name) + ' <span style="color:#888;">(' + sizeKB + 'KB)</span></div>';
                    html += '<div style="color:#555;font-size:0.7em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;">' + photo.name + '</div>';
                    html += '</div>';
                    html += '<div style="display:flex;gap:5px;flex-shrink:0;margin-left:10px;">';
                    html += '<button onclick="viewPhoto(\'' + photo.name + '\')" style="padding:5px 10px;background:linear-gradient(135deg,#00f0ff,#0099aa);color:#000;border:none;border-radius:5px;cursor:pointer;font-size:0.8em;font-weight:600;">Ver</button>';
                    html += '<button onclick="downloadPhoto(\'' + photo.name + '\')" style="padding:5px 10px;background:linear-gradient(135deg,#e0ff00,#aacc00);color:#000;border:none;border-radius:5px;cursor:pointer;font-size:0.8em;font-weight:600;">Descargar</button>';
                    html += '<button onclick="deletePhoto(\'' + photo.name + '\')" style="padding:5px 10px;background:linear-gradient(135deg,#ff0055,#aa0033);color:#fff;border:none;border-radius:5px;cursor:pointer;font-size:0.8em;font-weight:600;">Eliminar</button>';
                    html += '</div></div>';
                });
                gallery.innerHTML = html;
            } catch (error) {
                console.error('Error loading photos:', error);
            }
        }

        function viewPhoto(name) {
            const idx = photoList.findIndex(p => p.name === name);
            if (idx >= 0) {
                showViewer(idx);
            } else {
                window.open('/photo?folder=' + encodeURIComponent(currentFolder) + '&name=' + encodeURIComponent(name), '_blank');
            }
        }

        function showViewer(index) {
            if (index < 0 || index >= photoList.length) return;
            currentPhotoIndex = index;
            const photo = photoList[index];
            const viewer = document.getElementById('photoViewer');
            const img = document.getElementById('viewerImg');
            const nameEl = document.getElementById('viewerName');
            const rawEl = document.getElementById('viewerRaw');
            const counterEl = document.getElementById('viewerCounter');
            img.src = '/photo?folder=' + encodeURIComponent(currentFolder) + '&name=' + encodeURIComponent(photo.name);
            nameEl.textContent = formatPhotoDate(photo.name);
            rawEl.textContent = photo.name;
            counterEl.textContent = (index + 1) + ' / ' + photoList.length;
            document.getElementById('prevBtn').disabled = (index === 0);
            document.getElementById('nextBtn').disabled = (index === photoList.length - 1);
            viewer.classList.add('active');
        }

        function prevPhoto() {
            if (currentPhotoIndex > 0) showViewer(currentPhotoIndex - 1);
        }

        function nextPhoto() {
            if (currentPhotoIndex < photoList.length - 1) showViewer(currentPhotoIndex + 1);
        }

        function closeViewer() {
            document.getElementById('photoViewer').classList.remove('active');
            currentPhotoIndex = -1;
        }

        function downloadPhoto(name) {
            const a = document.createElement('a');
            a.href = '/photo?folder=' + encodeURIComponent(currentFolder) + '&name=' + encodeURIComponent(name) + '&dl=1';
            a.download = name;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
        }

        async function deletePhoto(name) {
            if (!confirm('Eliminar ' + name + '?')) return;
            try {
                const response = await fetch('/delete-photo', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ name: name, folder: currentFolder })
                });
                if (response.ok) {
                    showToast('Foto eliminada');
                    loadPhotos();
                    loadFolders();
                }
            } catch (error) {
                showToast('Error al eliminar');
            }
        }

        // Cargar configuracion al inicio
        loadSettings();
        loadStatus();
        loadFolders();
        loadPhotos();

        // Actualizar estado cada 5 segundos
        setInterval(loadStatus, 5000);
    </script>
</body>
</html>
)rawliteral";

    return html;
}
