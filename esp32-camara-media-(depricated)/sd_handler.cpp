#include "sd_handler.h"
#include "config.h"
#include <time.h>
#include <Preferences.h>

SDHandler sdCard;
static Preferences sdPrefs;

SDHandler::SDHandler() : initialized(false), photosFolder(DEFAULT_PHOTOS_FOLDER) {}

bool SDHandler::init() {
    // Inicializar SD_MMC en modo 1-bit para liberar GPIO4 (flash LED)
    if (!SD_MMC.begin("/sdcard", SD_MMC_1BIT_MODE)) {
        Serial.println("Error al montar tarjeta SD");
        initialized = false;
        return false;
    }

    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No se detectó tarjeta SD");
        initialized = false;
        return false;
    }

    Serial.print("Tipo de tarjeta SD: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("DESCONOCIDO");
    }

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("Tamaño de tarjeta SD: %lluMB\n", cardSize);

    // Cargar configuración guardada
    loadConfig();

    // Crear directorio principal
    createDirectory("/" + photosFolder);

    initialized = true;
    Serial.println("Tarjeta SD inicializada correctamente");
    Serial.printf("Carpeta de fotos: /%s\n", photosFolder.c_str());
    return true;
}

bool SDHandler::createDirectory(String path) {
    if (SD_MMC.exists(path)) {
        return true;
    }

    Serial.printf("Creando directorio: %s\n", path.c_str());
    if (SD_MMC.mkdir(path)) {
        Serial.println("Directorio creado");
        return true;
    } else {
        Serial.println("Error al crear directorio");
        return false;
    }
}

String SDHandler::getCurrentYearMonth() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "unknown";
    }

    char yearMonth[16];
    snprintf(yearMonth, sizeof(yearMonth), "%04d-%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1);

    return String(yearMonth);
}

void SDHandler::ensureMonthDirectory() {
    // Ya no se usa, las fotos van directo a la carpeta raíz
}

String SDHandler::generateFilename() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Error obteniendo hora local");
        return "/" + photosFolder + "/foto_" + String(millis()) + ".jpg";
    }

    // Formato: /carpeta/YYYY-MM-DD_HH-MM.jpg
    char filename[80];
    snprintf(filename, sizeof(filename),
             "/%s/%04d-%02d-%02d_%02d-%02d.jpg",
             photosFolder.c_str(),
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min);

    return String(filename);
}

String SDHandler::getCurrentDate() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "unknown";
    }

    char dateStr[16];
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday);

    return String(dateStr);
}

bool SDHandler::savePhoto(const uint8_t* data, size_t size, String filename) {
    if (!initialized) {
        Serial.println("SD no inicializada");
        return false;
    }

    if (filename.isEmpty()) {
        filename = generateFilename();
    }

    File file = SD_MMC.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("Error al abrir archivo para escritura");
        return false;
    }

    size_t bytesWritten = file.write(data, size);
    file.close();

    if (bytesWritten != size) {
        Serial.println("Error al escribir archivo");
        return false;
    }

    Serial.printf("Foto guardada: %s (%d bytes)\n", filename.c_str(), size);
    return true;
}

bool SDHandler::deletePhoto(String filename) {
    if (!initialized) {
        return false;
    }

    return SD_MMC.remove(filename);
}

uint8_t* SDHandler::readPhoto(String filename, size_t& size) {
    size = 0;

    if (!initialized) {
        Serial.println("SD no inicializada");
        return nullptr;
    }

    if (!SD_MMC.exists(filename)) {
        Serial.printf("Archivo no existe: %s\n", filename.c_str());
        return nullptr;
    }

    File file = SD_MMC.open(filename, FILE_READ);
    if (!file) {
        Serial.println("Error al abrir archivo para lectura");
        return nullptr;
    }

    size = file.size();
    if (size == 0) {
        Serial.println("Archivo vacío");
        file.close();
        return nullptr;
    }

    // Usar PSRAM si está disponible para archivos grandes
    uint8_t* buffer = nullptr;
    if (psramFound() && size > 10000) {
        buffer = (uint8_t*)ps_malloc(size);
    } else {
        buffer = (uint8_t*)malloc(size);
    }

    if (!buffer) {
        Serial.println("Error al asignar memoria para leer foto");
        file.close();
        size = 0;
        return nullptr;
    }

    size_t bytesRead = file.read(buffer, size);
    file.close();

    if (bytesRead != size) {
        Serial.printf("Error al leer archivo: %d de %d bytes\n", bytesRead, size);
        free(buffer);
        size = 0;
        return nullptr;
    }

    Serial.printf("Foto leida: %s (%d bytes)\n", filename.c_str(), size);
    return buffer;
}

void SDHandler::freePhotoBuffer(uint8_t* buffer) {
    if (buffer) {
        free(buffer);
    }
}

String SDHandler::getLatestPhoto() {
    if (!initialized) {
        return "";
    }

    String folderPath = "/" + photosFolder;
    File root = SD_MMC.open(folderPath);
    if (!root) {
        return "";
    }

    String latestFile = "";
    unsigned long latestTime = 0;

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(".jpg")) {
                time_t modTime = file.getLastWrite();
                if (modTime > latestTime) {
                    latestTime = modTime;
                    latestFile = folderPath + "/" + name;
                }
            }
        }
        file = root.openNextFile();
    }

    return latestFile;
}

String SDHandler::getDailyPhotoPath() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "/" + photosFolder + "/foto_unknown.jpg";
    }

    // Formato: /carpeta/YYYY-MM-DD_HH-MM.jpg
    char path[80];
    snprintf(path, sizeof(path), "/%s/%04d-%02d-%02d_%02d-%02d.jpg",
             photosFolder.c_str(),
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min);

    return String(path);
}

bool SDHandler::photoExistsToday() {
    if (!initialized) {
        return false;
    }

    // Buscar cualquier foto del día actual
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return false;
    }

    char datePrefix[20];
    snprintf(datePrefix, sizeof(datePrefix), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday);

    String folderPath = "/" + photosFolder;
    File dir = SD_MMC.open(folderPath);
    if (!dir || !dir.isDirectory()) {
        return false;
    }

    File file = dir.openNextFile();
    while (file) {
        String fileName = String(file.name());
        if (fileName.startsWith(datePrefix)) {
            file.close();
            dir.close();
            return true;
        }
        file = dir.openNextFile();
    }
    dir.close();
    return false;
}

String SDHandler::findPhotoByDate(int year, int month, int day) {
    if (!initialized) {
        return "";
    }

    // Crear prefijo de fecha a buscar: YYYY-MM-DD
    char datePrefix[16];
    snprintf(datePrefix, sizeof(datePrefix), "%04d-%02d-%02d", year, month, day);

    String folderPath = "/" + photosFolder;
    File dir = SD_MMC.open(folderPath);
    if (!dir || !dir.isDirectory()) {
        return "";
    }

    File file = dir.openNextFile();
    while (file) {
        String fileName = String(file.name());
        if (fileName.startsWith(datePrefix) && fileName.endsWith(".jpg")) {
            String fullPath = folderPath + "/" + fileName;
            file.close();
            dir.close();
            return fullPath;
        }
        file = dir.openNextFile();
    }
    dir.close();
    return "";
}

String SDHandler::listPhotos(int page, int perPage, int* totalPages) {
    if (!initialized) {
        if (totalPages) *totalPages = 0;
        return "";
    }

    String folderPath = "/" + photosFolder;
    File dir = SD_MMC.open(folderPath);
    if (!dir || !dir.isDirectory()) {
        if (totalPages) *totalPages = 0;
        return "";
    }

    // Recolectar archivos jpg (máximo 100 para no consumir mucha memoria)
    String files[100];
    int fileCount = 0;

    File file = dir.openNextFile();
    while (file && fileCount < 100) {
        if (!file.isDirectory()) {
            String fileName = String(file.name());
            if (fileName.endsWith(".jpg") || fileName.endsWith(".JPG")) {
                files[fileCount] = fileName;
                fileCount++;
            }
        }
        file = dir.openNextFile();
    }
    dir.close();

    if (fileCount == 0) {
        if (totalPages) *totalPages = 0;
        return "";
    }

    // Ordenar archivos alfabéticamente (más recientes primero por formato YYYY-MM-DD)
    for (int i = 0; i < fileCount - 1; i++) {
        for (int j = i + 1; j < fileCount; j++) {
            if (files[j] > files[i]) {
                String temp = files[i];
                files[i] = files[j];
                files[j] = temp;
            }
        }
    }

    // Calcular paginación
    int total = (fileCount + perPage - 1) / perPage;  // Redondear hacia arriba
    if (totalPages) *totalPages = total;

    // Validar página
    if (page < 1) page = 1;
    if (page > total) page = total;

    int startIndex = (page - 1) * perPage;
    int endIndex = min(startIndex + perPage, fileCount);

    // Construir lista formateada
    String result = "";
    for (int i = startIndex; i < endIndex; i++) {
        int num = i + 1;  // Número de la foto (1-indexed)
        String name = files[i];

        // Extraer fecha del nombre: YYYY-MM-DD_HH-MM.jpg -> DD/MM/YYYY HH:MM
        if (name.length() >= 16) {
            String year = name.substring(0, 4);
            String month = name.substring(5, 7);
            String day = name.substring(8, 10);
            String hour = name.substring(11, 13);
            String minute = name.substring(14, 16);
            result += String(num) + ". `" + day + "/" + month + "/" + year + "` - " + hour + ":" + minute + "\n";
        } else {
            result += String(num) + ". `" + name + "`\n";
        }
    }

    return result;
}

void SDHandler::setPhotosFolder(String folderName) {
    // Limpiar el nombre (sin / al inicio o final)
    folderName.trim();
    if (folderName.startsWith("/")) {
        folderName = folderName.substring(1);
    }
    if (folderName.endsWith("/")) {
        folderName = folderName.substring(0, folderName.length() - 1);
    }

    // Validar que no esté vacío
    if (folderName.isEmpty()) {
        folderName = DEFAULT_PHOTOS_FOLDER;
    }

    photosFolder = folderName;

    // Crear la carpeta
    if (initialized) {
        createDirectory("/" + photosFolder);
    }

    Serial.printf("Carpeta de fotos cambiada a: /%s\n", photosFolder.c_str());
}

String SDHandler::getPhotosFolder() {
    return photosFolder;
}

void SDHandler::saveConfig() {
    sdPrefs.begin("sdconfig", false);
    sdPrefs.putString("folder", photosFolder);
    sdPrefs.end();
    Serial.println("Configuracion de SD guardada");
}

void SDHandler::loadConfig() {
    sdPrefs.begin("sdconfig", true);
    photosFolder = sdPrefs.getString("folder", DEFAULT_PHOTOS_FOLDER);
    sdPrefs.end();
    Serial.printf("Configuracion SD cargada: carpeta = %s\n", photosFolder.c_str());
}

uint64_t SDHandler::getTotalSpace() {
    if (!initialized) return 0;
    return SD_MMC.totalBytes();
}

uint64_t SDHandler::getUsedSpace() {
    if (!initialized) return 0;
    return SD_MMC.usedBytes();
}

uint64_t SDHandler::getFreeSpace() {
    if (!initialized) return 0;
    return SD_MMC.totalBytes() - SD_MMC.usedBytes();
}

bool SDHandler::isInitialized() {
    return initialized;
}
