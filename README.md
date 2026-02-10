# ESP32-CAM Media Server con Bot de Telegram

Servidor multimedia para ESP32-CAM con dashboard web, streaming de video en vivo, bot de Telegram para control remoto y almacenamiento en tarjeta SD.

## Funcionalidades

- **Dashboard Web** - Interfaz visual con tema cyberpunk neon para controlar la camara desde el navegador
- **Streaming MJPEG** - Video en vivo accesible desde cualquier navegador en la red local
- **Bot de Telegram** - Control remoto completo: captura de fotos, configuracion, gestion de usuarios
- **Foto del dia** - Captura automatica programable con almacenamiento en SD y envio por Telegram
- **Tarjeta SD** - Almacenamiento local de fotos con organizacion por fecha
- **Configuracion por Serial** - Las credenciales (WiFi, token de Telegram, timezone) se configuran por puerto serial y se guardan en memoria NVS
- **Control de camara** - Brillo, contraste, saturacion, efectos, balance de blancos, exposicion, ganancia, calidad JPEG, resolucion y flash LED
- **Multi-usuario** - Soporte para hasta 10 usuarios autorizados en Telegram con roles (admin + usuarios)

## Hardware requerido

- **ESP32-CAM AI-Thinker** con sensor OV2640
- **Tarjeta microSD** (opcional, formato FAT32) para almacenamiento local
- **Programador FTDI** o adaptador USB-Serial para cargar el firmware

## Configuracion en Arduino IDE

### 1. Instalar soporte para ESP32

En Arduino IDE, ve a **Archivo > Preferencias** y agrega esta URL en "Gestor de URLs adicionales de tarjetas":

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Luego ve a **Herramientas > Placa > Gestor de tarjetas**, busca "esp32" e instala **esp32 by Espressif Systems**.

### 2. Instalar librerias

Desde **Herramientas > Administrar bibliotecas**, instala:

| Libreria | Autor | Version |
|----------|-------|---------|
| ArduinoJson | Benoit Blanchon | v6.x |
| UniversalTelegramBot | Brian Lough | v1.3.0+ |

### 3. Configurar la placa

En **Herramientas**, selecciona:

| Opcion | Valor |
|--------|-------|
| Board | AI Thinker ESP32-CAM |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| PSRAM | Enabled |
| Upload Speed | 921600 |
| CPU Frequency | 240MHz |

### 4. Cargar el firmware

1. Abre `esp32-camara-media/esp32-camara-media.ino` en Arduino IDE
2. Conecta el programador FTDI al ESP32-CAM (GPIO0 a GND para modo flash)
3. Selecciona el puerto COM correcto
4. Haz clic en **Subir**
5. Desconecta GPIO0 de GND y presiona RESET

## Configuracion inicial

Al encender por primera vez, el sistema solicita credenciales por puerto serial (115200 baud):

1. **WiFi SSID** - Nombre de tu red WiFi
2. **WiFi Password** - Contrasena de tu red WiFi
3. **Bot Token de Telegram** - Token obtenido de [@BotFather](https://t.me/BotFather)
4. **Timezone UTC** - Offset horario (ej: -5 para Colombia, +1 para Espana)

Las credenciales se guardan en memoria NVS y se reutilizan en reinicios posteriores. Para saltar la configuracion y usar las credenciales guardadas:
- Presiona **ENTER** en cada campo (usa el valor guardado)
- Conecta **GPIO15 a GND** al encender (bypass completo)

## Comandos de Telegram

El primer usuario que escriba al bot se convierte automaticamente en **administrador**.

### Fotos
| Comando | Descripcion |
|---------|-------------|
| `/foto` | Capturar y enviar foto |
| `/foto DD/MM/YYYY` | Enviar foto guardada de una fecha |

### Flash
| Comando | Descripcion |
|---------|-------------|
| `/flash on` | Activar flash LED |
| `/flash off` | Desactivar flash LED |

### Foto diaria
| Comando | Descripcion |
|---------|-------------|
| `/fotodiaria` | Enviar foto del dia guardada en SD |
| `/fotodiaria on` | Activar envio automatico por Telegram |
| `/fotodiaria off` | Desactivar envio (sigue guardando en SD) |
| `/config` | Ver configuracion actual |
| `/hora HH:MM` | Cambiar hora de la foto diaria |
| `/flashdiario` | Toggle flash para foto diaria |

### SD Card
| Comando | Descripcion |
|---------|-------------|
| `/carpeta` | Ver carpeta actual de fotos |
| `/carpeta nombre` | Cambiar carpeta de fotos |

### Usuarios (solo admin)
| Comando | Descripcion |
|---------|-------------|
| `/users` | Ver lista de usuarios autorizados |
| `/myid` | Ver tu ID de Telegram |
| `/add ID` | Agregar usuario autorizado |
| `/remove ID` | Eliminar usuario |

### Sistema
| Comando | Descripcion |
|---------|-------------|
| `/estado` | Ver estado del sistema (RAM, SD, WiFi, config) |
| `/stream` | Ver enlace de streaming |
| `/ip` | Ver direccion IP |
| `/reiniciar` | Reiniciar el ESP32-CAM |

## Dashboard Web

Accede desde el navegador a `http://<IP-del-ESP32>/` para:

- Ver streaming en vivo (MJPEG)
- Capturar fotos (se guardan en SD)
- Ajustar parametros de la camara en tiempo real
- Ver galeria de fotos capturadas con visor integrado
- Descargar y eliminar fotos
- Monitorear estado del sistema (RAM, PSRAM, SD)

## Endpoints HTTP

| Ruta | Metodo | Descripcion |
|------|--------|-------------|
| `/` | GET | Dashboard HTML |
| `/stream` | GET | Streaming MJPEG |
| `/capture` | GET | Capturar foto (JPEG) |
| `/web-capture` | GET | Capturar y guardar en SD |
| `/settings` | GET | Obtener configuracion de camara (JSON) |
| `/settings` | POST | Actualizar configuracion de camara (JSON) |
| `/status` | GET | Estado del sistema (JSON) |
| `/photos` | GET | Lista de fotos en SD (JSON) |
| `/photo?name=X` | GET | Ver foto especifica |
| `/photo?name=X&dl=1` | GET | Descargar foto |
| `/delete-photo` | POST | Eliminar foto (JSON: `{"name":"..."}`) |

## Estructura del proyecto

```
esp32-camara-media/
├── esp32-camara-media.ino   # Sketch principal (setup/loop)
├── config.h                 # Pines, constantes y configuracion
├── credentials_manager.h    # Gestion de credenciales (header)
├── credentials_manager.cpp  # Gestion de credenciales (NVS + serial)
├── camera_handler.h         # Control de camara (header)
├── camera_handler.cpp       # Inicializacion OV2640, captura, ajustes
├── web_server.h             # Servidor web (header)
├── web_server.cpp           # Dashboard, streaming, API REST
├── telegram_bot.h           # Bot de Telegram (header)
├── telegram_bot.cpp         # Comandos, foto diaria, multi-usuario
├── sd_handler.h             # Manejo de SD (header)
└── sd_handler.cpp           # Lectura/escritura SD, organizacion por fecha
```

## Esquema de conexion

```
ESP32-CAM AI-Thinker
┌─────────────────┐
│  OV2640 Camera   │
│                  │
│  GPIO 4  → Flash LED
│  GPIO 15 → Boton bypass (opcional, conectar a GND)
│                  │
│  SD Card Slot    │ (modo 1-bit para liberar GPIO4)
│                  │
│  3.3V, GND      │
│  U0T → RX FTDI  │
│  U0R → TX FTDI  │
│  GPIO0 → GND    │ (solo para programar)
└─────────────────┘
```

## Notas

- La tarjeta SD es **opcional**. Sin ella, el sistema funciona normalmente pero no guarda fotos localmente.
- Las fotos se guardan con formato `YYYY-MM-DD_HH-MM.jpg` en la carpeta configurada (por defecto `/fotos`).
- El flash LED (GPIO4) se comparte con la SD en modo 4-bit. Se usa modo **1-bit** para evitar conflictos.
- El sistema se reconecta automaticamente a WiFi si pierde conexion.
- La hora se sincroniza por NTP cada hora.
