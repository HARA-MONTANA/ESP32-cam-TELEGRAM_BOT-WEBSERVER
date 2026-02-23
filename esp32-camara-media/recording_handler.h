#ifndef RECORDING_HANDLER_H
#define RECORDING_HANDLER_H

#include <Arduino.h>
#include "FS.h"
#include "SD_MMC.h"

/*
 * RecordingHandler - Graba video en formato AVI/MJPEG en la tarjeta SD.
 *
 * El formato AVI con codec MJPEG concatena frames JPEG dentro de un
 * contenedor RIFF/AVI estándar. Los archivos resultantes (.avi) se pueden
 * reproducir en cualquier reproductor de video (VLC, Windows Media Player, etc.)
 *
 * Uso:
 *   recordingHandler.startRecording(fps);  // Iniciar grabación
 *   recordingHandler.update();             // Llamar en cada iteración del loop
 *   recordingHandler.stopRecording();      // Detener y guardar el archivo
 */
class RecordingHandler {
public:
    RecordingHandler();

    // Control de grabación
    bool startRecording(int fps = 10);
    bool stopRecording();
    bool isRecording() const;
    void update();  // Llamar en cada iteración del loop principal

    // Estado
    unsigned long getElapsedSeconds() const;
    int getFrameCount() const;
    String getCurrentFilename() const;
    String getStatusJSON();

    // Gestión de archivos
    String listRecordingsJSON();
    bool deleteRecording(String filename);

private:
    bool _isRecording;
    int _fps;
    uint32_t _frameInterval;       // Milisegundos entre frames (1000/fps)
    unsigned long _startTime;
    unsigned long _lastFrameTime;
    int _frameCount;
    String _currentFilename;
    File _aviFile;

    // Posiciones en el archivo para actualizar al finalizar
    uint32_t _riffSizeOffset;      // Offset 4: tamaño total RIFF - 8
    uint32_t _aviFramesOffset;     // Offset 48: dwTotalFrames en avih
    uint32_t _strhFramesOffset;    // Offset 140: dwLength en strh
    uint32_t _moviListSizeOffset;  // Offset 216: tamaño del chunk LIST movi
    uint32_t _totalBytes;          // Bytes totales escritos (= tamaño del archivo)

    int _width;
    int _height;

    // Escritura AVI
    bool writeAVIHeader(int width, int height, int fps);
    bool writeFrame(const uint8_t* data, size_t len);
    bool finalizeAVI();

    // Helpers de escritura binaria
    void write32LE(File& f, uint32_t v);
    void write16LE(File& f, uint16_t v);
    void writeFCC(File& f, const char* fcc);
    void seekAndWrite32LE(uint32_t offset, uint32_t value);
};

extern RecordingHandler recordingHandler;

#endif // RECORDING_HANDLER_H
