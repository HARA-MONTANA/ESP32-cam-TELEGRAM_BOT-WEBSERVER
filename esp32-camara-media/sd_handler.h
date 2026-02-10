#ifndef SD_HANDLER_H
#define SD_HANDLER_H

#include <Arduino.h>
#include "FS.h"
#include "SD_MMC.h"

class SDHandler {
public:
    SDHandler();

    bool init();
    bool savePhoto(const uint8_t* data, size_t size, String filename = "");
    bool deletePhoto(String filename);
    uint8_t* readPhoto(String filename, size_t& size);  // Lee foto de SD, caller debe liberar memoria con free()
    void freePhotoBuffer(uint8_t* buffer);              // Libera buffer de foto
    String getLatestPhoto();
    String getDailyPhotoPath();
    bool photoExistsToday();
    String findPhotoByDate(int year, int month, int day);  // Busca foto por fecha
    String listPhotos(int page = 1, int perPage = 10, int* totalPages = nullptr);  // Lista fotos con paginación

    // Configuración de carpeta
    void setPhotosFolder(String folderName);
    String getPhotosFolder();
    void saveConfig();
    void loadConfig();

    // Información de la SD
    uint64_t getTotalSpace();
    uint64_t getUsedSpace();
    uint64_t getFreeSpace();

    bool isInitialized();

private:
    bool initialized;
    String photosFolder;      // Nombre de la carpeta raíz

    String generateFilename();
    String getCurrentDate();
    String getCurrentYearMonth();  // Para organizar por mes
    bool createDirectory(String path);
    void ensureMonthDirectory();   // Crea carpeta del mes actual si no existe
};

extern SDHandler sdCard;

#endif // SD_HANDLER_H
