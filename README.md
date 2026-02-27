# ESP32-CAM Media Server con Bot de Telegram y Bot de Discord

Servidor multimedia para ESP32-CAM con dashboard web, streaming de video en vivo, bot de Telegram, bot de Discord y almacenamiento en tarjeta SD.

## Funcionalidades

- **Dashboard Web** - Interfaz visual con tema cyberpunk neon para controlar la camara desde el navegador
- **Streaming MJPEG** - Video en vivo accesible desde cualquier navegador en la red local
- **Bot de Telegram** - Control remoto completo: captura de fotos, configuracion, gestion de usuarios
- **Bot de Discord** - Control remoto desde Discord: fotos, fotos con flash, foto diaria, grabacion de video y estado del sistema
- **Grabacion de video** - El bot de Discord puede grabar clips MP4 directamente desde el stream MJPEG (hasta 30 segundos)
- **Foto del dia** - Captura automatica programable con almacenamiento en SD y envio por Telegram o Discord
- **Tarjeta SD** - Almacenamiento local de fotos con organizacion por fecha
- **Configuracion por Serial** - Las credenciales (WiFi, token de Telegram, timezone) se configuran por puerto serial y se guardan en memoria NVS
- **Control de camara** - Brillo, contraste, saturacion, efectos, balance de blancos, exposicion, ganancia, calidad JPEG, resolucion y flash LED
- **Multi-usuario** - Soporte para hasta 10 usuarios autorizados en Telegram con roles (admin + usuarios)
- **Multi-red WiFi** - Guarda hasta 5 redes WiFi y cambia automaticamente si pierde conexion
- **Modo ahorro de energia** - Reduce consumo ~40-50% en reposo activando WiFi modem sleep y reduciendo polling de Telegram

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
- Conecta **GPIO13 a GND** al encender (bypass completo)

### Multi-red WiFi

El sistema soporta hasta **5 redes WiFi guardadas**. Si pierde conexion a la red activa, prueba automaticamente las demas en orden. Puedes agregar redes adicionales durante la configuracion serial.

## Comandos de Telegram

El primer usuario que escriba al bot se convierte automaticamente en **administrador**.

### General
| Comando | Descripcion |
|---------|-------------|
| `/start` | Bienvenida e instrucciones |
| `/ayuda` | Ver todos los comandos disponibles |

### Fotos
| Comando | Descripcion |
|---------|-------------|
| `/foto` | Capturar y enviar foto |
| `/foto N` | Enviar foto por numero (ver `/carpeta`) |
| `/carpeta` | Ver todas las fotos guardadas en la SD |
| `/carpeta N` | Ver pagina N de la lista de fotos |
| `/enviar N` | Enviar foto N de la lista |

### Flash
| Comando | Descripcion |
|---------|-------------|
| `/flash on` | Activar flash LED (aplica a fotos y foto diaria) |
| `/flash off` | Desactivar flash LED (aplica a fotos y foto diaria) |

### Foto diaria
| Comando | Descripcion |
|---------|-------------|
| `/fotodiaria` | Enviar foto del dia guardada en SD |
| `/fotodiaria on` | Activar envio automatico por Telegram |
| `/fotodiaria off` | Desactivar envio (sigue guardando en SD) |
| `/config` | Ver configuracion actual |
| `/hora HH:MM` | Cambiar hora de la foto diaria |

### Ahorro de energia
| Comando | Descripcion |
|---------|-------------|
| `/dormir` | Activar modo sleep manualmente |
| `/despertar` | Salir del modo sleep |
| `/sleepconfig` | Ver configuracion de sleep |
| `/sleepconfig N` | Cambiar timeout de inactividad (minutos, 0 = desactivado) |

### Usuarios (solo admin)
| Comando | Descripcion |
|---------|-------------|
| `/users` | Ver lista de usuarios autorizados |
| `/myid` | Ver tu ID de Telegram |
| `/add ID` | Agregar usuario autorizado |
| `/remove ID` | Eliminar usuario |
| `/admin ID` | Dar permisos de admin a un usuario |

### Sistema
| Comando | Descripcion |
|---------|-------------|
| `/estado` | Ver estado del sistema (RAM, SD, WiFi, config) |
| `/stream` | Ver enlace de streaming |
| `/ip` | Ver direccion IP |
| `/reiniciar` | Reiniciar el ESP32-CAM |

## Bot de Discord

El bot de Discord es un programa Python independiente que corre en tu PC o servidor y se comunica con la ESP32-CAM a traves de la red local.

### Comandos disponibles

Los comandos funcionan tanto como **slash commands** (`/foto`) como con el **prefix de texto** (`w!foto`):

| Comando | Descripcion |
|---------|-------------|
| `/foto` | Captura una imagen en vivo y la envia al canal |
| `/foto_flash` | Captura una imagen con el flash LED (GPIO4) encendido |
| `/fotodiaria` | Envia la foto automatica del dia (la busca en la SD; si no hay, captura en vivo) |
| `/video [segundos]` | Graba un clip MP4 del stream MJPEG y lo envia (1–30 segundos, default 10) |
| `/estado` | Muestra RAM libre, PSRAM, senal WiFi, red, uptime e IP de la camara |
| `/help` | Muestra la lista de todos los comandos |

### Requisitos previos

- Python 3.10 o superior
- La ESP32-CAM encendida y conectada a la misma red WiFi que el PC
- FFmpeg **no** es necesario; la grabacion de video usa OpenCV directamente

### Configuracion paso a paso

#### 1. Crear el bot en Discord Developer Portal

1. Ve a [https://discord.com/developers/applications](https://discord.com/developers/applications) e inicia sesion.
2. Haz clic en **New Application**, dale un nombre (ej. `ESP32-CAM`) y confirma.
3. En el panel izquierdo ve a **Bot** y haz clic en **Add Bot** → **Yes, do it!**.
4. En la seccion **Token**, haz clic en **Reset Token** y copia el token generado.
   > Guarda este token en un lugar seguro; no lo compartas ni lo subas a Git.
5. Desplaza hacia abajo hasta **Privileged Gateway Intents** y activa:
   - **Message Content Intent**
6. Guarda los cambios con **Save Changes**.

#### 2. Invitar el bot a tu servidor

1. En el panel izquierdo ve a **OAuth2 > URL Generator**.
2. En **Scopes** marca: `bot` y `applications.commands`.
3. En **Bot Permissions** marca: `Send Messages`, `Attach Files`, `Embed Links`, `Read Message History`.
4. Copia la URL generada, pegala en el navegador e invita el bot a tu servidor.

#### 3. Instalar dependencias Python

```bash
cd discord_bot
pip install -r requirements.txt
```

Las dependencias son:

| Paquete | Uso |
|---------|-----|
| `discord.py >= 2.3.2` | Libreria principal del bot (slash commands + texto) |
| `requests >= 2.31.0` | Peticiones HTTP a la ESP32-CAM |
| `python-dotenv >= 1.0.0` | Carga de variables desde el archivo `.env` |
| `opencv-python >= 4.8.0` | Grabacion del stream MJPEG y escritura de MP4 |
| `numpy >= 1.24.0` | Decodificacion de frames JPEG en memoria |

#### 4. Configurar las credenciales

Copia el archivo de ejemplo y editalo:

```bash
cp .env.example .env
```

Abre `.env` con cualquier editor de texto y completa los valores:

```env
# Token del bot (obtenido en el paso 1)
DISCORD_TOKEN=TU_TOKEN_AQUI

# IP local de la ESP32-CAM (consultala en el monitor serie o en tu router)
ESP32_IP=192.168.1.100

# Puerto del servidor web de la camara (por defecto 80)
ESP32_PORT=80

# Prefix para comandos de texto (los slash commands / siempre funcionan)
COMMAND_PREFIX=w!
```

Alternativamente, puedes usar el **menu interactivo** para configurar sin editar el archivo:

```bash
python main.py
# Selecciona la opcion 1 → Configurar credenciales
```

#### 5. Iniciar el bot

**Con el menu interactivo (recomendado):**

```bash
python main.py
# Selecciona la opcion 2 → Iniciar bot de Discord
```

**Directamente:**

```bash
python main.py
```

Cuando el bot arranque veras en la terminal:

```
HH:MM:SS [INFO] Bot conectado como ESP32-CAM#1234 (ID: xxxxxxxxxx)
HH:MM:SS [INFO] ESP32-CAM → http://192.168.1.100:80
HH:MM:SS [INFO] Sincronizados 6 comandos slash
```

Los slash commands pueden tardar hasta **1 hora** en aparecer globalmente en Discord tras la primera sincronizacion.

#### 6. Grabar video sin iniciar el bot

El menu tambien permite grabar clips directamente desde la terminal sin necesidad de Discord:

```bash
python main.py
# Selecciona la opcion 3 → Grabar video desde el stream
```

Los videos se guardan en la carpeta `discord_bot/recordings/`.

### Estructura del bot de Discord

```
discord_bot/
├── main.py           # Menu interactivo (punto de entrada)
├── bot.py            # Comandos de Discord (/foto, /video, /estado, etc.)
├── recorder.py       # Grabacion de video desde el stream MJPEG (OpenCV)
├── requirements.txt  # Dependencias Python
└── .env.example      # Plantilla de configuracion (copia a .env)
```

### Notas del bot de Discord

- El archivo `.env` **nunca debe subirse a Git** (ya esta en `.gitignore`).
- El bot y la ESP32-CAM deben estar en la **misma red local** (WiFi). No funciona de forma remota a menos que uses un tunel como ngrok o expongas el puerto en el router.
- El limite de archivos adjuntos en Discord sin Nitro Boost es **25 MB**. Si un video supera ese limite, el bot lo indica y sugiere reducir la duracion.
- La senal WiFi se clasifica automaticamente: Excelente (> -60 dBm), Buena (> -70 dBm), Regular (> -80 dBm), Debil (<= -80 dBm).

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
| `/flash?state=on\|off` | GET | Activar/desactivar flash LED |
| `/settings` | GET | Obtener configuracion de camara (JSON) |
| `/settings` | POST | Actualizar configuracion de camara (JSON) |
| `/status` | GET | Estado del sistema (JSON) |
| `/photos` | GET | Lista de fotos en SD (JSON) |
| `/photo?name=X` | GET | Ver foto especifica |
| `/photo?name=X&dl=1` | GET | Descargar foto |
| `/delete-photo` | POST | Eliminar foto (JSON: `{"name":"..."}`) |

## Estructura del proyecto

```
ESP32-cam-TELEGRAM_BOT-WEBSERVER/
├── esp32-camara-media/
│   ├── esp32-camara-media.ino   # Sketch principal (setup/loop)
│   ├── config.h                 # Pines, constantes y configuracion
│   ├── credentials_manager.h    # Gestion de credenciales (header)
│   ├── credentials_manager.cpp  # Gestion de credenciales (NVS + serial + multi-WiFi)
│   ├── camera_handler.h         # Control de camara (header)
│   ├── camera_handler.cpp       # Inicializacion OV2640, captura, ajustes
│   ├── web_server.h             # Servidor web (header)
│   ├── web_server.cpp           # Dashboard, streaming, API REST
│   ├── telegram_bot.h           # Bot de Telegram (header)
│   ├── telegram_bot.cpp         # Comandos, foto diaria, multi-usuario
│   ├── sd_handler.h             # Manejo de SD (header)
│   ├── sd_handler.cpp           # Lectura/escritura SD, organizacion por fecha
│   ├── sleep_manager.h          # Modo ahorro de energia (header)
│   └── sleep_manager.cpp        # WiFi modem sleep, polling adaptativo
└── discord_bot/
    ├── main.py                  # Menu interactivo (punto de entrada)
    ├── bot.py                   # Comandos de Discord
    ├── recorder.py              # Grabacion de video MJPEG con OpenCV
    ├── requirements.txt         # Dependencias Python
    └── .env.example             # Plantilla de configuracion
```

## Esquema de conexion

```
ESP32-CAM AI-Thinker
┌─────────────────┐
│  OV2640 Camera   │
│                  │
│  GPIO 4  → Flash LED
│  GPIO 13 → Boton bypass (opcional, conectar a GND)
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
- Las fotos se organizan en carpetas: `/fotos_diarias` (foto automatica), `/fotos_telegram` (capturadas por Telegram) y `/fotos_web` (capturadas desde el dashboard web). El formato de nombre es `YYYY-MM-DD_HH-MM-SS.jpg`.
- El flash LED (GPIO4) se comparte con la SD en modo 4-bit. Se usa modo **1-bit** para evitar conflictos.
- El sistema se reconecta automaticamente a WiFi si pierde conexion, probando todas las redes guardadas en orden circular con backoff exponencial.
- La hora se sincroniza por NTP cada hora.
- El **modo sleep** activa WiFi modem sleep tras N minutos de inactividad (por defecto 10 min). El web server sigue activo; cualquier conexion web o mensaje de Telegram despierta el sistema automaticamente.
