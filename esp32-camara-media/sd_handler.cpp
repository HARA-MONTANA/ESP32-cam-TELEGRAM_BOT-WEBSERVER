#include "sd_handler.h"
#include "config.h"
#include <time.h>

SDHandler sdCard;

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

    uint64_t cardSizeMB = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("Tamaño de tarjeta SD: %.2f GB\n", cardSizeMB / 1024.0);

    // Crear directorio principal
    createDirectory("/" + photosFolder);

    // Crear directorio para fotos de Telegram
    createDirectory("/" + String(TELEGRAM_PHOTOS_FOLDER));

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
    return findPhotoInFolder(photosFolder, year, month, day);
}

String SDHandler::findPhotoInFolder(String folder, int year, int month, int day) {
    if (!initialized) {
        return "";
    }

    // Crear prefijo de fecha a buscar: YYYY-MM-DD
    char datePrefix[16];
    snprintf(datePrefix, sizeof(datePrefix), "%04d-%02d-%02d", year, month, day);

    String folderPath = "/" + folder;
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
    return listPhotosInFolder(photosFolder, page, perPage, totalPages);
}

String SDHandler::listPhotosInFolder(String folder, int page, int perPage, int* totalPages) {
    if (!initialized) {
        if (totalPages) *totalPages = 0;
        return "";
    }

    String folderPath = "/" + folder;
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

    // Ordenar archivos alfabéticamente (más antiguos primero por formato YYYY-MM-DD)
    for (int i = 0; i < fileCount - 1; i++) {
        for (int j = i + 1; j < fileCount; j++) {
            if (files[j] < files[i]) {
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

// Recolecta fotos de una carpeta en un array (helper interno)
static int collectPhotosFromFolder(String folderPath, String* files, int maxFiles) {
    File dir = SD_MMC.open(folderPath);
    if (!dir || !dir.isDirectory()) return 0;

    int count = 0;
    File file = dir.openNextFile();
    while (file && count < maxFiles) {
        if (!file.isDirectory()) {
            String fileName = String(file.name());
            if (fileName.endsWith(".jpg") || fileName.endsWith(".JPG")) {
                files[count] = folderPath + "/" + fileName;
                count++;
            }
        }
        file = dir.openNextFile();
    }
    dir.close();

    // Ordenar alfabéticamente ascendente (más antiguos primero)
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (files[j] < files[i]) {
                String temp = files[i];
                files[i] = files[j];
                files[j] = temp;
            }
        }
    }
    return count;
}

// Formatea nombre de archivo a fecha legible
static String formatPhotoEntry(String fullPath, int num) {
    // Extraer solo el nombre del archivo
    int lastSlash = fullPath.lastIndexOf('/');
    String name = (lastSlash >= 0) ? fullPath.substring(lastSlash + 1) : fullPath;

    // Manejar prefijo web_
    String suffix = "";
    String datePart = name;
    if (name.startsWith("web_")) {
        suffix = " (web)";
        datePart = name.substring(4);
    }

    // Parsear fecha: YYYY-MM-DD_HH-MM-SS.jpg o YYYY-MM-DD_HH-MM.jpg
    if (datePart.length() >= 16) {
        String year = datePart.substring(0, 4);
        String month = datePart.substring(5, 7);
        String day = datePart.substring(8, 10);
        String hour = datePart.substring(11, 13);
        String minute = datePart.substring(14, 16);
        String second = "";
        if (datePart.length() >= 19 && datePart.charAt(16) == '-') {
            second = ":" + datePart.substring(17, 19);
        }
        return String(num) + ". " + day + "/" + month + "/" + year + " " + hour + ":" + minute + second + suffix;
    }
    return String(num) + ". " + name;
}

// Prioridad de carpetas para ordenar en listado
static int getFolderPriority(const String& name) {
    if (name == DEFAULT_PHOTOS_FOLDER) return 0;   // fotos_diarias
    if (name == TELEGRAM_PHOTOS_FOLDER) return 1;   // fotos_telegram
    if (name == WEB_PHOTOS_FOLDER) return 2;         // fotos_web
    return 3;  // Otras carpetas al final
}

// Estructura para info de carpeta (uso interno)
struct FolderInfo {
    String name;
    int startIndex;
    int count;
};

#define MAX_SD_FOLDERS 10
#define MAX_TOTAL_PHOTOS 100

// Recolecta todas las fotos de todas las carpetas en la SD
static int collectAllPhotosFromSD(String* allPhotos, FolderInfo* folders, int* folderCount) {
    *folderCount = 0;
    int totalPhotos = 0;

    // Recolectar nombres de directorios
    String dirNames[MAX_SD_FOLDERS];
    int dirCount = 0;

    File root = SD_MMC.open("/");
    if (!root || !root.isDirectory()) return 0;

    File entry = root.openNextFile();
    while (entry && dirCount < MAX_SD_FOLDERS) {
        if (entry.isDirectory()) {
            String name = String(entry.name());
            if (name.startsWith("/")) name = name.substring(1);
            if (!name.isEmpty() && !name.startsWith(".") && name != "System Volume Information") {
                dirNames[dirCount] = name;
                dirCount++;
            }
        }
        entry = root.openNextFile();
    }
    root.close();

    // Ordenar directorios por prioridad: fotos_diarias, fotos_telegram, fotos_web, resto alfabético
    for (int i = 0; i < dirCount - 1; i++) {
        for (int j = i + 1; j < dirCount; j++) {
            int pi = getFolderPriority(dirNames[i]);
            int pj = getFolderPriority(dirNames[j]);
            if (pj < pi || (pj == pi && dirNames[j] < dirNames[i])) {
                String temp = dirNames[i];
                dirNames[i] = dirNames[j];
                dirNames[j] = temp;
            }
        }
    }

    // Recolectar fotos de cada directorio
    for (int d = 0; d < dirCount && totalPhotos < MAX_TOTAL_PHOTOS; d++) {
        int maxForThis = MAX_TOTAL_PHOTOS - totalPhotos;
        int collected = collectPhotosFromFolder("/" + dirNames[d], allPhotos + totalPhotos, maxForThis);

        if (collected > 0) {
            folders[*folderCount].name = dirNames[d];
            folders[*folderCount].startIndex = totalPhotos;
            folders[*folderCount].count = collected;
            (*folderCount)++;
            totalPhotos += collected;
        }
    }

    return totalPhotos;
}

String SDHandler::listAllPhotosTree(int page, int perPage, int* totalPages) {
    if (!initialized) {
        if (totalPages) *totalPages = 0;
        return "";
    }

    // Recolectar fotos de todas las carpetas de la SD
    String allPhotos[MAX_TOTAL_PHOTOS];
    FolderInfo folders[MAX_SD_FOLDERS];
    int folderCount = 0;

    int totalPhotos = collectAllPhotosFromSD(allPhotos, folders, &folderCount);

    if (totalPhotos == 0) {
        if (totalPages) *totalPages = 0;
        return "";
    }

    // Calcular paginación sobre el total (página 1 = fotos más recientes)
    int total = (totalPhotos + perPage - 1) / perPage;
    if (totalPages) *totalPages = total;
    if (page < 1) page = 1;
    if (page > total) page = total;

    // Paginación inversa: página 1 muestra las fotos más recientes (números más altos)
    int startIndex = totalPhotos - page * perPage;
    if (startIndex < 0) startIndex = 0;
    int endIndex = totalPhotos - (page - 1) * perPage;
    if (endIndex > totalPhotos) endIndex = totalPhotos;

    String result = "";

    // Iterar carpetas en reversa para mostrar primero las que tienen fotos más recientes
    for (int f = folderCount - 1; f >= 0; f--) {
        int folderStart = folders[f].startIndex;
        int folderEnd = folderStart + folders[f].count;

        // Saltar carpetas fuera del rango de la página actual
        if (folderEnd <= startIndex || folderStart >= endIndex) continue;

        // Encabezado de carpeta
        result += "/" + folders[f].name + " (" + String(folders[f].count) + " fotos):\n";

        // Mostrar fotos en orden inverso (más reciente primero), manteniendo numeración original
        for (int i = folderEnd - 1; i >= folderStart; i--) {
            if (i >= startIndex && i < endIndex) {
                result += formatPhotoEntry(allPhotos[i], i + 1) + "\n";  // 1-indexed
            }
        }
        result += "\n";
    }

    return result;
}

String SDHandler::getPhotoPathByIndex(int index) {
    if (!initialized || index < 1) return "";

    // Recolectar fotos de todas las carpetas (mismo orden que listAllPhotosTree)
    String allPhotos[MAX_TOTAL_PHOTOS];
    FolderInfo folders[MAX_SD_FOLDERS];
    int folderCount = 0;

    int totalPhotos = collectAllPhotosFromSD(allPhotos, folders, &folderCount);

    if (index > totalPhotos) return "";
    return allPhotos[index - 1];  // Convertir 1-indexed a 0-indexed
}

int SDHandler::countAllPhotos() {
    if (!initialized) return 0;

    int count = 0;

    // Contar fotos en todas las carpetas de la SD
    File root = SD_MMC.open("/");
    if (!root || !root.isDirectory()) return 0;

    File dirEntry = root.openNextFile();
    while (dirEntry) {
        if (dirEntry.isDirectory()) {
            String dirName = String(dirEntry.name());
            if (dirName.startsWith("/")) dirName = dirName.substring(1);
            if (!dirName.isEmpty() && !dirName.startsWith(".") && dirName != "System Volume Information") {
                File dir = SD_MMC.open("/" + dirName);
                if (dir && dir.isDirectory()) {
                    File f = dir.openNextFile();
                    while (f) {
                        if (!f.isDirectory()) {
                            String name = String(f.name());
                            if (name.endsWith(".jpg") || name.endsWith(".JPG")) count++;
                        }
                        f = dir.openNextFile();
                    }
                    dir.close();
                }
            }
        }
        dirEntry = root.openNextFile();
    }
    root.close();

    return count;
}

String SDHandler::getPhotosFolder() {
    return photosFolder;
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
