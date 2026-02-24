#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>

class CameraWebServer {
public:
    CameraWebServer(int port = 80);

    void init();
    void handleClient();

private:
    WebServer server;

    // Handlers de rutas - fotos
    void handleRoot();
    void handleStream();
    void handleCapture();
    void handleSettings();
    void handleGetSettings();
    void handleUpdateSettings();
    void handleStatus();
    void handleWebCapture();
    void handleListPhotos();
    void handleListFolders();
    void handleViewPhoto();
    void handleDeletePhoto();

    // Handlers de rutas - grabaciones de video
    void handleStartRecording();
    void handleStopRecording();
    void handleRecordingStatus();
    void handleListRecordings();
    void handleDownloadRecording();
    void handleDeleteRecording();

    // Handlers de rutas - gestión WiFi
    void handleGetWiFiNetworks();
    void handleAddWiFiNetwork();
    void handleUpdateWiFiNetwork();
    void handleDeleteWiFiNetwork();
    void handleGetWiFiStatus();

    void handleNotFound();

    // Generar HTML del dashboard
    String generateDashboardHTML();
};

extern CameraWebServer webServer;

#endif // WEB_SERVER_H
