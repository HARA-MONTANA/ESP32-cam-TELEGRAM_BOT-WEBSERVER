# ESP32-CAM Media Server con Bot de Telegram

Sistema completo de captura de fotos y streaming de video para ESP32-CAM con integración de Telegram, dashboard web interactivo y soporte experimental de Discord.

## Funcionalidades

- **Captura de fotos** desde la cámara OV2640
- **Streaming de video en vivo** (MJPEG) a través de página web
- **Dashboard web** con tema cyberpunk neon y controles visuales para ajustar la cámara
- **Galería de fotos** integrada en el dashboard con visor y navegación prev/next
- **Bot de Telegram** para control remoto:
  - Tomar y recibir fotos al instante
  - Foto del día automática
  - Control del flash
  - Ver estado del sistema
  - Gestión multi-usuario con roles (admin/usuarios)
- **Almacenamiento en SD Card** de fotos diarias y capturas web
- **Configuración dinámica** por serial al primer inicio (WiFi, token, zona horaria)
- **Configuración persistente** (se guarda en memoria flash)
- **Bot de Discord** (experimental, solo PlatformIO)

## Hardware Requerido

- ESP32-CAM (AI-Thinker) con cámara OV2640
- Tarjeta microSD (opcional, FAT32, máximo 32GB, para almacenamiento local)
- Programador FTDI o similar

## Instalación

### 1. Prerrequisitos

- [PlatformIO](https://platformio.org/) (recomendado) o Arduino IDE
- Cuenta de Telegram

### 2. Configurar el Bot de Telegram

1. Abre Telegram y busca `@BotFather`
2. Envía `/newbot` y sigue las instrucciones
3. Copia el **token** que te da BotFather
4. Para obtener tu **Chat ID**:
   - Busca `@userinfobot` en Telegram
   - Envía `/start` y te dará tu ID

### 3. Configurar el Bot de Discord (Opcional)

Si deseas usar el bot de Discord (solo PlatformIO con `-DDISCORD_ENABLED`):

1. Ve al [Portal de Desarrolladores de Discord](https://discord.com/developers/applications)
2. Crea una nueva aplicación y luego un **Bot**
3. Copia el **Token** del bot
4. Activa los **Privileged Gateway Intents** necesarios (Message Content Intent)
5. Invita el bot a tu servidor con permisos de lectura/escritura de mensajes

### 4. Configurar el Proyecto

El sistema utiliza configuración dinámica por serial. Al iniciar por primera vez:

1. **Conecta el ESP32-CAM** y abre el Monitor Serial (115200 baud)
2. **Ingresa las credenciales** cuando se soliciten:
   - WiFi SSID
   - WiFi Password
   - Bot Token de Telegram
   - Zona horaria (offset en horas, ej: -5 para UTC-5)
   - **Si Discord está habilitado** (`-DDISCORD_ENABLED`):
     - Discord Bot Token

**Opciones de configuración:**
- Presiona **ENTER** para usar el último valor guardado
- Cada campo tiene timeout de **30 segundos** con contador visual
- Presiona el **botón en PIN 15** (LOW) para saltar la configuración y usar valores guardados

**Valores opcionales en `include/config.h`:**

```cpp
// Hora de la foto diaria (formato 24h) - se puede cambiar via Telegram
#define DAILY_PHOTO_HOUR 11
#define DAILY_PHOTO_MINUTE 0
#define DAILY_PHOTO_FLASH false  // Flash para foto diaria

// Carpeta de fotos en SD
#define DEFAULT_PHOTOS_FOLDER "fotos"
```

> **Nota:** Ya no es necesario configurar CHAT_ID manualmente. El primer usuario que escriba al bot se registrará automáticamente como administrador.

### 5. Compilar y Subir

#### Opción A: PlatformIO (Recomendado)

```bash
pio run -t upload
```

Todas las dependencias se resuelven automáticamente desde `platformio.ini`.

#### Opción B: Arduino IDE

1. **Instalar soporte ESP32:**
   - Abre Arduino IDE
   - Ve a `Archivo > Preferencias`
   - En "URLs adicionales de Gestor de Tarjetas" agrega:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Ve a `Herramientas > Placa > Gestor de tarjetas`
   - Busca "esp32" e instala "esp32 by Espressif Systems"

2. **Instalar librerías requeridas:**
   - Ve a `Herramientas > Administrar Bibliotecas`
   - Busca e instala:
     - **ArduinoJson** by Benoit Blanchon (versión 6.x)
     - **UniversalTelegramBot** by Brian Lough

3. **Preparar el proyecto:**
   - Abre el archivo `esp32-camara-media.ino` de la raíz del proyecto
   - Copia los archivos `.h` de `include/` y `.cpp` de `src/` a la misma carpeta del `.ino`
   - **No copies** `discord_bot.h` ni `discord_bot.cpp` (son solo para PlatformIO)

4. **Configurar la placa:**
   - `Herramientas > Placa > esp32 > AI Thinker ESP32-CAM`
   - `Herramientas > Partition Scheme > Huge APP (3MB No OTA/1MB SPIFFS)`
   - `Herramientas > PSRAM > Enabled`
   - `Herramientas > Puerto > [Tu puerto COM/ttyUSB]`

5. **Subir el sketch:**
   - Conecta el ESP32-CAM en modo programación (GPIO0 a GND)
   - Presiona el botón de reset
   - Click en "Subir"
   - Cuando termine, desconecta GPIO0 de GND y reinicia

## Uso

### Dashboard Web

Una vez conectado, accede a:
- **Dashboard**: `http://[IP_ESP32]/`
- **Stream**: `http://[IP_ESP32]/stream`
- **Captura**: `http://[IP_ESP32]/capture`

El dashboard permite:
- Ver streaming en vivo de la cámara
- Ajustar brillo, contraste y saturación con sliders
- Cambiar resolución de imagen en tiempo real (QVGA a UXGA)
- Ajustar calidad JPEG
- Aplicar efectos especiales (sepia, negativo, B/N, etc.)
- Configurar balance de blancos (auto, soleado, nublado, oficina, hogar)
- Controlar flash LED
- Ajustar exposición y ganancia automática
- **Capturar fotos** y guardarlas directamente en la SD Card
- **Galería de fotos** con visor integrado y navegación anterior/siguiente
- **Eliminar fotos** desde la interfaz web

### Comandos del Bot de Telegram

#### Fotos
| Comando | Alias | Descripción |
|---------|-------|-------------|
| `/foto` | `/photo`, `/captura` | Capturar y enviar foto (no guarda en SD) |
| `/foto DD/MM/YYYY` | `/photo DD/MM/YYYY` | Enviar foto guardada de una fecha (ej: `/foto 05/01/2026`) |

#### Flash
| Comando | Descripción |
|---------|-------------|
| `/flash on` | Activar flash |
| `/flash off` | Desactivar flash |

#### Foto Diaria
| Comando | Alias | Descripción |
|---------|-------|-------------|
| `/fotodiaria` | - | Enviar la foto del día guardada en SD |
| `/fotodiaria on` | - | Activar envío automático a Telegram |
| `/fotodiaria off` | - | Desactivar envío (sigue guardando en SD) |
| `/config` | `/configuracion` | Ver configuración actual |
| `/hora HH:MM` | `/sethour`, `/settime` | Cambiar hora (ej: `/hora 11:30`) |
| `/flashdiario` | `/dailyflash` | Toggle activar/desactivar flash |
| `/flashdiarion` | `/dailyflashon` | Activar flash para foto diaria |
| `/flashdiarioff` | `/dailyflashoff` | Desactivar flash para foto diaria |

#### SD Card
| Comando | Alias | Descripción |
|---------|-------|-------------|
| `/carpeta` | `/folder` | Ver carpeta actual de fotos |
| `/carpeta nombre` | `/folder nombre` | Cambiar carpeta de fotos |

#### Gestión de Usuarios
| Comando | Alias | Descripción |
|---------|-------|-------------|
| `/users` | `/ids` | Ver lista de usuarios autorizados |
| `/myid` | - | Ver tu propio ID de Telegram |
| `/add ID` | `/adduser ID` | Agregar usuario autorizado (solo admin) |
| `/remove ID` | `/removeuser ID`, `/del ID` | Eliminar usuario (solo admin) |

#### Sistema
| Comando | Alias | Descripción |
|---------|-------|-------------|
| `/estado` | `/status` | Ver estado del sistema |
| `/stream` | - | Ver enlace de streaming |
| `/ip` | - | Ver dirección IP |
| `/reiniciar` | `/restart`, `/reboot` | Reiniciar ESP32-CAM |
| `/ayuda` | `/help`, `/start` | Ver lista de comandos |

### Sistema de Usuarios y Autorización

El bot implementa un sistema de gestión de usuarios múltiples con roles:

**Características:**
- **Auto-registro del primer usuario**: El primer usuario que escriba al bot se convierte automáticamente en **administrador**
- **Soporte multi-usuario**: Hasta 10 usuarios autorizados simultáneamente
- **Roles**: Admin (primer usuario) y usuarios normales
- **Persistencia**: Los IDs autorizados se guardan en memoria flash

**Flujo de configuración inicial:**
1. Sube el código al ESP32-CAM
2. El primer usuario que envíe cualquier mensaje al bot será registrado como admin
3. El admin puede agregar más usuarios con `/add ID`

**Comandos de gestión (solo admin):**
- `/add 123456789` - Autorizar nuevo usuario por su ID
- `/remove 123456789` - Eliminar usuario autorizado
- `/users` - Ver lista completa de usuarios

**Para obtener un ID de Telegram:**
- Busca `@userinfobot` en Telegram y envía `/start`

### Foto del Día Automática

El sistema captura automáticamente una foto a la hora configurada (por defecto 11:00 AM).

**Comportamiento:**
- La foto se guarda **siempre** en la SD Card a la hora programada
- El envío a Telegram es **opcional** (controlar con `/fotodiaria on/off`)

**Comandos:**
- `/foto` - Toma una foto y la envía (no guarda en SD)
- `/fotodiaria` - Envía la foto del día guardada en SD
- `/fotodiaria on` - Activa envío automático
- `/fotodiaria off` - Desactiva envío (sigue guardando)
- `/hora 14:30` - Cambia hora de captura
- `/flashdiario` - Activa/desactiva flash
- `/config` - Ver configuración

La configuración se guarda en memoria flash y persiste después de reiniciar.

### Organización de Fotos en SD

Las fotos diarias y las capturas desde el dashboard web se guardan en la SD:

```
/fotos/                          (carpeta configurable con /carpeta)
├── 2026-01-15_11-00.jpg         (foto diaria a las 11:00)
├── 2026-01-16_11-00.jpg
└── 2026-01-17_14-30.jpg

/fotos_tomadas/                  (capturas desde el dashboard web)
└── 2026-01-17_15-45.jpg
```

- Formato: `YYYY-MM-DD_HH-MM.jpg`
- El comando `/foto` solo envía por Telegram, no guarda en SD
- Cambiar carpeta de fotos diarias: `/carpeta vigilancia` → `/vigilancia/`

### Bot de Discord (Experimental)

El proyecto incluye soporte para un bot de Discord con funcionalidad completa. Esta funcionalidad:

- **Solo disponible con PlatformIO** (requiere flag `-DDISCORD_ENABLED`)
- **No compatible con Arduino IDE**
- Requiere la librería [`esp-discord`](https://github.com/abobija/esp-discord)
- Prefijo de comandos: `w!`

Para habilitar, agrega en `platformio.ini`:
```ini
build_flags =
    -DDISCORD_ENABLED
```

#### Comandos del Bot de Discord

##### Fotos
| Comando | Alias | Descripción |
|---------|-------|-------------|
| `w!foto` | `w!photo`, `w!captura` | Capturar y enviar foto |
| `w!foto DD/MM/YYYY` | `w!photo DD/MM/YYYY` | Enviar foto guardada de una fecha |
| `w!galeria` | `w!fotos` | Ver galería de fotos con paginación |

##### Foto Diaria
| Comando | Descripción |
|---------|-------------|
| `w!fotodiaria` | Enviar la foto del día guardada en SD |
| `w!fotodiaria on` | Activar envío automático a Discord |
| `w!fotodiaria off` | Desactivar envío (sigue guardando en SD) |
| `w!config` | Ver configuración actual |

##### Sistema
| Comando | Alias | Descripción |
|---------|-------|-------------|
| `w!estado` | `w!status` | Ver estado del sistema |
| `w!help` | - | Ver lista de comandos |

**Nota:** El bot de Discord es abierto - cualquier usuario en el servidor puede usar los comandos.

## Estructura del Proyecto

```
esp32-camara-media/
├── esp32-camara-media.ino       # Wrapper para Arduino IDE
├── platformio.ini               # Configuración PlatformIO
├── include/                     # Headers
│   ├── config.h                 # Pines, constantes, valores por defecto
│   ├── credentials_manager.h    # Gestión de credenciales WiFi/bot
│   ├── camera_handler.h         # Control de cámara y ajustes
│   ├── web_server.h             # Servidor HTTP y dashboard
│   ├── telegram_bot.h           # Bot de Telegram
│   ├── sd_handler.h             # Operaciones con SD Card
│   └── discord_bot.h            # Bot de Discord (solo PlatformIO)
├── src/                         # Código fuente
│   ├── main.cpp                 # Punto de entrada, inicialización
│   ├── credentials_manager.cpp  # Configuración por serial, persistencia
│   ├── camera_handler.cpp       # Captura, streaming, ajustes de cámara
│   ├── web_server.cpp           # Endpoints HTTP, dashboard HTML/CSS/JS
│   ├── telegram_bot.cpp         # Comandos, envío de fotos, gestión usuarios
│   ├── sd_handler.cpp           # Lectura/escritura SD, organización de fotos
│   └── discord_bot.cpp          # Integración Discord (experimental)
└── README.md
```

## API REST

El servidor web expone los siguientes endpoints:

| Endpoint | Método | Descripción |
|----------|--------|-------------|
| `/` | GET | Dashboard HTML con controles y galería |
| `/stream` | GET | Stream MJPEG en vivo |
| `/capture` | GET | Capturar foto (JPEG) |
| `/settings` | GET | Obtener configuración de cámara (JSON) |
| `/settings` | POST | Actualizar configuración de cámara (JSON) |
| `/status` | GET | Estado del sistema |
| `/web-capture` | GET | Capturar foto y guardar en SD Card |
| `/photos` | GET | Listar fotos guardadas en SD (JSON) |
| `/photo` | GET | Ver/descargar foto específica (`?name=...`) |
| `/delete-photo` | POST | Eliminar foto de la SD Card |

### Ejemplo: Actualizar Configuración

```bash
curl -X POST http://[IP]/settings \
  -H "Content-Type: application/json" \
  -d '{"brightness": 1, "contrast": 0, "flash": true}'
```

### Ejemplo: Listar Fotos

```bash
curl http://[IP]/photos
```

## Secuencia de Inicio

1. Inicialización del puerto serial (115200 baud)
2. Solicitud de credenciales por serial (o uso de guardadas con botón GPIO 15)
3. Inicialización de la cámara (con soporte PSRAM)
4. Inicialización de SD Card (opcional, continúa si falla)
5. Conexión WiFi (reintenta hasta 30 veces)
6. Sincronización NTP (pool.ntp.org)
7. Inicio del servidor web y bot de Telegram
8. Sistema listo - muestra IP y URLs en serial

## Solución de Problemas

### La cámara no inicia
- Verifica que el módulo ESP32-CAM tenga PSRAM habilitado
- Revisa las conexiones de la cámara

### No conecta a WiFi
- Verifica el SSID y contraseña ingresados por serial
- Acerca el módulo al router
- Reinicia para volver a ingresar credenciales

### El bot no responde
- Verifica el token del bot ingresado por serial
- Asegúrate de estar autorizado (el primer mensaje registra como admin)
- Revisa el Monitor Serial para ver mensajes de error

### SD Card no funciona
- Formatea la SD en FAT32
- Usa una tarjeta de máximo 32GB
- El sistema funciona sin SD (no guarda fotos localmente)

### El stream se congela
- Accede al dashboard y reconecta el stream
- El flash LED se apaga automáticamente al desconectar el stream
- Verifica la señal WiFi (RSSI visible en `/estado`)

## Librerías Utilizadas

| Librería | Versión | Uso |
|----------|---------|-----|
| ArduinoJson | ^6.21.3 | Serialización JSON para API y configuración |
| UniversalTelegramBot | ^1.3.0 | Comunicación con API de Telegram |
| WiFi | Built-in | Conectividad WiFi |
| WebServer | Built-in | Servidor HTTP |
| SD_MMC | Built-in | Acceso a tarjeta SD (modo 1-bit) |
| SPIFFS | Built-in | Sistema de archivos en flash |
| Preferences | Built-in | Almacenamiento persistente de configuración |

## Licencia

MIT License
