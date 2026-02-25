#include "recording_handler.h"
#include "config.h"
#include "camera_handler.h"
#include "sd_handler.h"
#include "esp_camera.h"
#include <time.h>

RecordingHandler recordingHandler;

// ============================================================
// Estructura del archivo AVI generado (MJPEG en contenedor RIFF):
//
// Offset   Contenido
// 0        'RIFF'  (4)
// 4        riff_size (4) ← se actualiza al finalizar
// 8        'AVI '  (4)
// 12       'LIST'  (4)
// 16       192     (4) = tamaño contenido hdrl
// 20       'hdrl'  (4)
// 24       'avih'  (4)
// 28       56      (4) = tamaño avih data
// 32-87    avih data (56 bytes):
//   48       dwTotalFrames (4) ← se actualiza al finalizar
// 88       'LIST'  (4)
// 92       116     (4) = tamaño contenido strl
// 96       'strl'  (4)
// 100      'strh'  (4)
// 104      56      (4)
// 108-163  strh data (56 bytes):
//   140      dwLength (4) ← se actualiza al finalizar
// 164      'strf'  (4)
// 168      40      (4)
// 172-211  BITMAPINFOHEADER (40 bytes)
// 212      'LIST'  (4)
// 216      movi_size (4) ← se actualiza al finalizar
// 220      'movi'  (4)
// 224+     frames: '00dc' (4) + size (4) + JPEG data
// ============================================================

RecordingHandler::RecordingHandler()
    : _isRecording(false), _fps(10), _frameInterval(100),
      _startTime(0), _lastFrameTime(0), _frameCount(0),
      _riffSizeOffset(0), _aviFramesOffset(0),
      _strhFramesOffset(0), _moviListSizeOffset(0),
      _totalBytes(0), _width(0), _height(0),
      _frameIndex(nullptr), _maxFrames(0) {}

// ---------- Helpers de escritura binaria ----------

void RecordingHandler::write32LE(File& f, uint32_t v) {
    uint8_t buf[4] = { (uint8_t)(v), (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    f.write(buf, 4);
    _totalBytes += 4;
}

void RecordingHandler::write16LE(File& f, uint16_t v) {
    uint8_t buf[2] = { (uint8_t)(v), (uint8_t)(v >> 8) };
    f.write(buf, 2);
    _totalBytes += 2;
}

void RecordingHandler::writeFCC(File& f, const char* fcc) {
    f.write((const uint8_t*)fcc, 4);
    _totalBytes += 4;
}

void RecordingHandler::seekAndWrite32LE(uint32_t offset, uint32_t value) {
    _aviFile.seek(offset);
    uint8_t buf[4] = { (uint8_t)(value), (uint8_t)(value >> 8),
                       (uint8_t)(value >> 16), (uint8_t)(value >> 24) };
    _aviFile.write(buf, 4);
    // No modificamos _totalBytes aquí, esta función actualiza valores ya escritos
}

// ---------- Cabecera AVI ----------

bool RecordingHandler::writeAVIHeader(int width, int height, int fps) {
    uint32_t usPerFrame    = 1000000UL / (uint32_t)fps;
    uint32_t maxBytesPerSec = (uint32_t)fps * 15000UL;  // estimado

    // --- RIFF header ---
    writeFCC(_aviFile, "RIFF");
    _riffSizeOffset = _totalBytes;        // offset 4
    write32LE(_aviFile, 0);               // placeholder
    writeFCC(_aviFile, "AVI ");

    // --- LIST hdrl ---
    writeFCC(_aviFile, "LIST");
    write32LE(_aviFile, 192);             // hdrl content: 4+64+124 = 192
    writeFCC(_aviFile, "hdrl");

    // avih chunk (56 bytes)
    writeFCC(_aviFile, "avih");
    write32LE(_aviFile, 56);
    write32LE(_aviFile, usPerFrame);      // dwMicroSecPerFrame   offset 32
    write32LE(_aviFile, maxBytesPerSec);  // dwMaxBytesPerSec     offset 36
    write32LE(_aviFile, 0);               // dwPaddingGranularity offset 40
    write32LE(_aviFile, 0x10);            // dwFlags: AVIF_HASINDEX offset 44
    _aviFramesOffset = _totalBytes;       //                      offset 48
    write32LE(_aviFile, 0);               // dwTotalFrames (placeholder)
    write32LE(_aviFile, 0);               // dwInitialFrames      offset 52
    write32LE(_aviFile, 1);               // dwStreams             offset 56
    write32LE(_aviFile, maxBytesPerSec);  // dwSuggestedBufferSize offset 60
    write32LE(_aviFile, (uint32_t)width); // dwWidth              offset 64
    write32LE(_aviFile, (uint32_t)height);// dwHeight             offset 68
    write32LE(_aviFile, 0);               // dwReserved[0]        offset 72
    write32LE(_aviFile, 0);               // dwReserved[1]        offset 76
    write32LE(_aviFile, 0);               // dwReserved[2]        offset 80
    write32LE(_aviFile, 0);               // dwReserved[3]        offset 84

    // --- LIST strl ---
    writeFCC(_aviFile, "LIST");           // offset 88
    write32LE(_aviFile, 116);             // strl content: 4+64+48 = 116
    writeFCC(_aviFile, "strl");

    // strh chunk (56 bytes)
    writeFCC(_aviFile, "strh");           // offset 100
    write32LE(_aviFile, 56);
    writeFCC(_aviFile, "vids");           // fccType              offset 108
    writeFCC(_aviFile, "MJPG");           // fccHandler           offset 112
    write32LE(_aviFile, 0);               // dwFlags              offset 116
    write16LE(_aviFile, 0);               // wPriority            offset 120
    write16LE(_aviFile, 0);               // wLanguage            offset 122
    write32LE(_aviFile, 0);               // dwInitialFrames      offset 124
    write32LE(_aviFile, 1);               // dwScale              offset 128
    write32LE(_aviFile, (uint32_t)fps);   // dwRate               offset 132
    write32LE(_aviFile, 0);               // dwStart              offset 136
    _strhFramesOffset = _totalBytes;      //                      offset 140
    write32LE(_aviFile, 0);               // dwLength (placeholder)
    write32LE(_aviFile, maxBytesPerSec);  // dwSuggestedBufferSize offset 144
    write32LE(_aviFile, 0xFFFFFFFF);      // dwQuality            offset 148
    write32LE(_aviFile, 0);               // dwSampleSize         offset 152
    write16LE(_aviFile, 0);               // rcFrame.left         offset 156
    write16LE(_aviFile, 0);               // rcFrame.top          offset 158
    write16LE(_aviFile, (uint16_t)width); // rcFrame.right        offset 160
    write16LE(_aviFile, (uint16_t)height);// rcFrame.bottom       offset 162

    // strf chunk = BITMAPINFOHEADER (40 bytes)
    writeFCC(_aviFile, "strf");           // offset 164
    write32LE(_aviFile, 40);
    write32LE(_aviFile, 40);              // biSize               offset 172
    write32LE(_aviFile, (uint32_t)width); // biWidth              offset 176
    write32LE(_aviFile, (uint32_t)height);// biHeight             offset 180
    write16LE(_aviFile, 1);               // biPlanes             offset 184
    write16LE(_aviFile, 24);              // biBitCount           offset 186
    writeFCC(_aviFile, "MJPG");           // biCompression        offset 188
    write32LE(_aviFile, (uint32_t)(width * height * 3)); // biSizeImage
    write32LE(_aviFile, 0);               // biXPelsPerMeter      offset 196
    write32LE(_aviFile, 0);               // biYPelsPerMeter      offset 200
    write32LE(_aviFile, 0);               // biClrUsed            offset 204
    write32LE(_aviFile, 0);               // biClrImportant       offset 208

    // --- LIST movi ---
    writeFCC(_aviFile, "LIST");           // offset 212
    _moviListSizeOffset = _totalBytes;    //                      offset 216
    write32LE(_aviFile, 0);               // placeholder
    writeFCC(_aviFile, "movi");           // offset 220
    // Frames empiezan en offset 224

    _aviFile.flush();
    return true;
}

// ---------- Escritura de frames ----------

bool RecordingHandler::writeFrame(const uint8_t* data, size_t len) {
    if (!_aviFile) return false;

    // Guardar offset del frame en el índice (antes de escribir el chunk)
    if (_frameIndex && _frameCount < _maxFrames) {
        // dwOffset en idx1: posición del chunk '00dc' relativa al inicio de movi data (byte 224)
        _frameIndex[_frameCount].offset = _totalBytes - 224;
        _frameIndex[_frameCount].size   = (uint32_t)len;
    }

    // Chunk '00dc': tag (4) + tamaño (4) + datos JPEG
    writeFCC(_aviFile, "00dc");
    write32LE(_aviFile, (uint32_t)len);
    _aviFile.write(data, len);
    _totalBytes += len;

    // AVI requiere chunks de tamaño par
    if (len % 2 != 0) {
        uint8_t pad = 0;
        _aviFile.write(&pad, 1);
        _totalBytes += 1;
    }

    _frameCount++;
    return true;
}

// ---------- Finalización ----------

bool RecordingHandler::finalizeAVI() {
    if (!_aviFile) return false;

    // Tamaño del movi ANTES de añadir idx1 (para el campo LIST movi size)
    uint32_t sizeBeforeIdx1 = _totalBytes;

    // Escribir idx1 al final del archivo (fuera del LIST movi)
    if (_frameIndex && _frameCount > 0) {
        writeFCC(_aviFile, "idx1");
        write32LE(_aviFile, (uint32_t)_frameCount * 16);  // 16 bytes por entrada
        for (int i = 0; i < _frameCount; i++) {
            writeFCC(_aviFile, "00dc");
            write32LE(_aviFile, 0x00000010);              // AVIIF_KEYFRAME
            write32LE(_aviFile, _frameIndex[i].offset);
            write32LE(_aviFile, _frameIndex[i].size);
        }
    }
    free(_frameIndex);
    _frameIndex = nullptr;

    uint32_t totalFileSize = _totalBytes;

    // Confirmar todos los datos al SD antes de actualizar la cabecera.
    // En algunas versiones del SDK de ESP32, el modo FILE_WRITE no garantiza
    // que los seeks hacia atrás funcionen correctamente sobre el buffer de escritura.
    // La solución robusta es cerrar el archivo aquí y reabrirlo en modo "r+"
    // (lectura/escritura sin truncar), que sí soporta seek+write confiable,
    // igual que hace tryRepairAVI().
    _aviFile.flush();
    _aviFile.close();

    File f = SD_MMC.open(_currentFilename, "r+");
    if (!f) {
        Serial.println("[REC] Error al reabrir AVI para actualizar cabecera");
        return false;
    }

    // Actualizar los cuatro campos placeholder escritos con valor 0 al inicio
    auto seekWrite32 = [&](uint32_t offset, uint32_t value) {
        uint8_t buf[4] = { (uint8_t)value, (uint8_t)(value >> 8),
                           (uint8_t)(value >> 16), (uint8_t)(value >> 24) };
        f.seek(offset);
        f.write(buf, 4);
    };

    // RIFF chunk size = tamaño total - 8 (incluye idx1)
    seekWrite32(_riffSizeOffset, totalFileSize - 8);

    // movi LIST size: solo los frames, sin idx1 (totalBytes antes de idx1 - 220)
    seekWrite32(_moviListSizeOffset, sizeBeforeIdx1 - 220);

    // Total frames en avih y strh
    seekWrite32(_aviFramesOffset, (uint32_t)_frameCount);
    seekWrite32(_strhFramesOffset, (uint32_t)_frameCount);

    f.flush();
    f.close();

    Serial.printf("[REC] Finalizado: %d frames, %u bytes\n", _frameCount, totalFileSize);
    return true;
}

// ---------- API pública ----------

bool RecordingHandler::startRecording(int fps) {
    if (_isRecording) {
        Serial.println("[REC] Ya hay una grabacion activa");
        return false;
    }
    if (!sdCard.isInitialized()) {
        Serial.println("[REC] SD no disponible");
        return false;
    }

    // Capturar primer frame para obtener dimensiones
    camera_fb_t* fb = camera.capturePhoto(false);
    if (!fb) {
        Serial.println("[REC] Error al capturar frame inicial");
        return false;
    }

    _width  = fb->width;
    _height = fb->height;

    // Crear directorio si no existe
    if (!SD_MMC.exists("/" RECORDINGS_FOLDER)) {
        SD_MMC.mkdir("/" RECORDINGS_FOLDER);
    }

    // Nombre de archivo con timestamp
    struct tm timeinfo;
    String filename;
    if (getLocalTime(&timeinfo)) {
        char buf[80];
        snprintf(buf, sizeof(buf), "/%s/REC_%04d-%02d-%02d_%02d-%02d-%02d.avi",
                 RECORDINGS_FOLDER,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        filename = String(buf);
    } else {
        filename = "/" + String(RECORDINGS_FOLDER) + "/REC_" + String(millis()) + ".avi";
    }

    _aviFile = SD_MMC.open(filename, FILE_WRITE);
    if (!_aviFile) {
        Serial.printf("[REC] Error al crear archivo: %s\n", filename.c_str());
        camera.releaseFrame(fb);
        return false;
    }

    _currentFilename = filename;
    _fps             = (fps < 1) ? 1 : (fps > 15 ? 15 : fps);
    _frameInterval   = 1000UL / (uint32_t)_fps;
    _frameCount      = 0;
    _totalBytes      = 0;

    // Alocar índice de frames en PSRAM (para idx1 al finalizar)
    _maxFrames = MAX_RECORDING_SECONDS * _fps + 10;
    _frameIndex = (FrameEntry*)heap_caps_malloc(_maxFrames * sizeof(FrameEntry),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_frameIndex) {
        _frameIndex = (FrameEntry*)malloc(_maxFrames * sizeof(FrameEntry));
    }
    if (!_frameIndex) {
        Serial.println("[REC] Sin memoria para idx1, grabacion sin indice de frames");
    }

    // Escribir cabecera AVI
    if (!writeAVIHeader(_width, _height, _fps)) {
        _aviFile.close();
        camera.releaseFrame(fb);
        Serial.println("[REC] Error al escribir cabecera AVI");
        return false;
    }

    // Escribir el primer frame capturado
    writeFrame(fb->buf, fb->len);
    camera.releaseFrame(fb);

    _startTime     = millis();
    _lastFrameTime = millis();
    _isRecording   = true;

    Serial.printf("[REC] Iniciada: %s (%dx%d @ %dfps)\n",
                  filename.c_str(), _width, _height, _fps);
    return true;
}

bool RecordingHandler::stopRecording() {
    if (!_isRecording) {
        Serial.println("[REC] No hay grabacion activa");
        return false;
    }

    _isRecording = false;

    if (_frameCount > 0) {
        finalizeAVI();  // finalizeAVI libera _frameIndex internamente
        Serial.printf("[REC] Guardada: %s (%d frames)\n",
                      _currentFilename.c_str(), _frameCount);
    } else {
        // Sin frames: eliminar archivo vacío
        free(_frameIndex);
        _frameIndex = nullptr;
        _aviFile.close();
        SD_MMC.remove(_currentFilename);
        _currentFilename = "";
        Serial.println("[REC] Grabacion sin frames, archivo eliminado");
    }

    return true;
}

bool RecordingHandler::isRecording() const {
    return _isRecording;
}

void RecordingHandler::update() {
    if (!_isRecording) return;

    unsigned long now = millis();

    // Auto-stop por tiempo máximo
    if ((now - _startTime) >= ((unsigned long)MAX_RECORDING_SECONDS * 1000UL)) {
        Serial.println("[REC] Tiempo maximo alcanzado, deteniendo...");
        stopRecording();
        return;
    }

    // Auto-stop por espacio insuficiente en SD
    if (sdCard.getFreeSpace() < ((uint64_t)MIN_FREE_SD_MB_FOR_RECORDING * 1024ULL * 1024ULL)) {
        Serial.println("[REC] Espacio SD insuficiente, deteniendo...");
        stopRecording();
        return;
    }

    // Verificar si es momento del siguiente frame
    if ((now - _lastFrameTime) < _frameInterval) return;

    _lastFrameTime = now;

    // Capturar y escribir frame
    camera_fb_t* fb = camera.capturePhoto(false);
    if (!fb) {
        Serial.println("[REC] Error capturando frame");
        return;
    }

    writeFrame(fb->buf, fb->len);
    camera.releaseFrame(fb);
}

unsigned long RecordingHandler::getElapsedSeconds() const {
    if (!_isRecording) return 0;
    return (millis() - _startTime) / 1000UL;
}

int RecordingHandler::getFrameCount() const {
    return _frameCount;
}

String RecordingHandler::getCurrentFilename() const {
    return _currentFilename;
}

String RecordingHandler::getStatusJSON() {
    String json = "{";
    json += "\"recording\":" + String(_isRecording ? "true" : "false");
    if (_isRecording) {
        json += ",\"elapsed\":" + String(getElapsedSeconds());
        json += ",\"frames\":" + String(_frameCount);
        String fname = _currentFilename.substring(_currentFilename.lastIndexOf('/') + 1);
        json += ",\"filename\":\"" + fname + "\"";
    }
    json += "}";
    return json;
}

String RecordingHandler::listRecordingsJSON() {
    String folder = "/" + String(RECORDINGS_FOLDER);
    File dir = SD_MMC.open(folder);
    if (!dir || !dir.isDirectory()) return "[]";

    // Recopilar nombres y tamaños
    String names[50];
    uint32_t sizes[50];
    int count = 0;

    File file = dir.openNextFile();
    while (file && count < 50) {
        if (!file.isDirectory()) {
            String name = String(file.name());
            if (name.endsWith(".avi") || name.endsWith(".AVI")) {
                names[count] = name;
                sizes[count] = file.size();
                count++;
            }
        }
        file = dir.openNextFile();
    }
    dir.close();

    // Ordenar por nombre descendente (más recientes primero)
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (names[j] > names[i]) {
                String tmpN = names[i]; names[i] = names[j]; names[j] = tmpN;
                uint32_t tmpS = sizes[i]; sizes[i] = sizes[j]; sizes[j] = tmpS;
            }
        }
    }

    String json = "[";
    for (int i = 0; i < count; i++) {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + names[i] + "\",\"size\":" + String(sizes[i]) + "}";
    }
    json += "]";
    return json;
}

bool RecordingHandler::deleteRecording(String filename) {
    if (filename.indexOf("..") >= 0) return false;
    String path = "/" + String(RECORDINGS_FOLDER) + "/" + filename;
    return SD_MMC.remove(path);
}

// ---------- Reparación de archivos sin finalizar ----------

bool RecordingHandler::tryRepairAVI(const String& path) {
    // Abrir en modo lectura/escritura sin truncar
    File f = SD_MMC.open(path.c_str(), "r+");
    if (!f) return false;

    size_t fileSize = f.size();
    if (fileSize < 224) {
        f.close();
        SD_MMC.remove(path.c_str());
        Serial.printf("[REC] Archivo demasiado pequeño eliminado: %s\n", path.c_str());
        return false;
    }

    // Leer dwTotalFrames en offset 48; si != 0 ya está finalizado
    uint8_t buf[4];
    f.seek(48);
    f.read(buf, 4);
    uint32_t totalFrames = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8)
                         | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    if (totalFrames != 0) {
        f.close();
        return false;  // ya finalizado correctamente
    }

    // Contar frames desde offset 224 escaneando chunks '00dc'
    uint32_t pos = 224;
    int frameCount = 0;
    uint8_t tag[4];
    while (pos + 8 <= (uint32_t)fileSize) {
        f.seek(pos);
        if (f.read(tag, 4) != 4) break;
        if (tag[0] != '0' || tag[1] != '0' || tag[2] != 'd' || tag[3] != 'c') break;

        if (f.read(buf, 4) != 4) break;
        uint32_t chunkSize = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8)
                           | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

        if (pos + 8 + chunkSize > (uint32_t)fileSize) break;  // chunk truncado
        frameCount++;
        pos += 8 + chunkSize;
        if (chunkSize % 2) pos++;  // padding AVI
    }

    if (frameCount == 0) {
        f.close();
        SD_MMC.remove(path.c_str());
        Serial.printf("[REC] AVI sin frames eliminado: %s\n", path.c_str());
        return false;
    }

    // Reparar: actualizar RIFF size (offset 4)
    uint32_t v = (uint32_t)fileSize - 8;
    buf[0] = v; buf[1] = v >> 8; buf[2] = v >> 16; buf[3] = v >> 24;
    f.seek(4);
    f.write(buf, 4);

    // movi LIST size (offset 216): desde 'movi' FCC hasta fin de datos = fileSize - 220
    v = (uint32_t)fileSize - 220;
    buf[0] = v; buf[1] = v >> 8; buf[2] = v >> 16; buf[3] = v >> 24;
    f.seek(216);
    f.write(buf, 4);

    // dwTotalFrames en avih (offset 48)
    v = (uint32_t)frameCount;
    buf[0] = v; buf[1] = v >> 8; buf[2] = v >> 16; buf[3] = v >> 24;
    f.seek(48);
    f.write(buf, 4);

    // dwLength en strh (offset 140)
    f.seek(140);
    f.write(buf, 4);

    f.flush();
    f.close();
    Serial.printf("[REC] AVI reparado: %s (%d frames)\n", path.c_str(), frameCount);
    return true;
}

void RecordingHandler::repairRecordings() {
    if (!sdCard.isInitialized()) return;

    String folder = "/" + String(RECORDINGS_FOLDER);
    File dir = SD_MMC.open(folder);
    if (!dir || !dir.isDirectory()) return;

    // Recopilar rutas primero para no interferir con el iterador del directorio
    String paths[50];
    int count = 0;
    File file = dir.openNextFile();
    while (file && count < 50) {
        if (!file.isDirectory()) {
            String name = String(file.name());
            // file.name() puede devolver ruta completa o solo nombre según versión de SDK
            String path = name.startsWith("/") ? name : (folder + "/" + name);
            if (path.endsWith(".avi") || path.endsWith(".AVI")) {
                paths[count++] = path;
            }
        }
        file = dir.openNextFile();
    }
    dir.close();

    for (int i = 0; i < count; i++) {
        tryRepairAVI(paths[i]);
    }
}
