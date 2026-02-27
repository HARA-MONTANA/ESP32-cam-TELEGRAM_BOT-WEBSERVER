"""
Bot de Discord para ESP32-CAM
==============================
Comandos disponibles (/comando o !comando):

  /foto          — Captura y envía una imagen en vivo
  /foto_flash    — Captura con el flash LED encendido (GPIO4)
  /fotodiaria    — Envía la foto automática del día (busca en SD, o captura en vivo)
  /video [seg]   — Graba N segundos del stream y envía el .mp4 (máx. 30 s)
  /estado        — Muestra RAM, WiFi, SD y uptime de la ESP32-CAM
  /ayuda         — Muestra esta ayuda

Configuración (archivo .env):
  DISCORD_TOKEN  — Token del bot de Discord (obligatorio)
  ESP32_IP       — IP local de la cámara  (default: 192.168.1.100)
  ESP32_PORT     — Puerto del servidor web (default: 80)
"""

import asyncio
import io
import logging
import os
import tempfile
from datetime import date, datetime

import discord
import requests
from discord import app_commands
from discord.ext import commands
from dotenv import load_dotenv

from recorder import record_stream

log = logging.getLogger("esp32-discord-bot")

# Valores de configuración — se rellenan en run() con _load_config()
DISCORD_TOKEN: str = ""
ESP32_IP: str = "192.168.1.100"
ESP32_PORT: str = "80"

MAX_VIDEO_SECONDS: int = 30
REQUEST_TIMEOUT: int = 10

# ---------------------------------------------------------------------------
# Configuración
# ---------------------------------------------------------------------------


def _load_config() -> None:
    """Carga (o recarga) las variables de entorno desde el .env."""
    global DISCORD_TOKEN, ESP32_IP, ESP32_PORT
    load_dotenv(override=True)
    DISCORD_TOKEN = os.getenv("DISCORD_TOKEN", "")
    ESP32_IP = os.getenv("ESP32_IP", "192.168.1.100")
    ESP32_PORT = os.getenv("ESP32_PORT", "80")


# ---------------------------------------------------------------------------
# Helpers HTTP → ESP32-CAM
# ---------------------------------------------------------------------------


def esp32_url(path: str = "") -> str:
    return f"http://{ESP32_IP}:{ESP32_PORT}{path}"


def capture_image(flash: bool = False) -> bytes | None:
    """Captura un JPEG desde /capture. Activa el flash si se indica."""
    if flash:
        _set_flash("on")
    try:
        r = requests.get(esp32_url("/capture"), timeout=REQUEST_TIMEOUT)
        r.raise_for_status()
        if "image" in r.headers.get("content-type", ""):
            return r.content
        log.warning("Respuesta de /capture no es imagen: %s", r.headers.get("content-type"))
        return None
    except requests.RequestException as exc:
        log.error("Error capturando imagen: %s", exc)
        return None
    finally:
        if flash:
            _set_flash("off")


def _set_flash(state: str) -> None:
    """Envía el comando de flash al endpoint /flash?state=on|off."""
    try:
        requests.get(esp32_url(f"/flash?state={state}"), timeout=5)
    except requests.RequestException:
        pass  # No bloquear si el flash falla


def get_status() -> dict | None:
    """Consulta /status y devuelve el JSON del sistema."""
    try:
        r = requests.get(esp32_url("/status"), timeout=REQUEST_TIMEOUT)
        r.raise_for_status()
        return r.json()
    except Exception as exc:
        log.error("Error obteniendo estado: %s", exc)
        return None


def get_daily_photo() -> tuple[bytes | None, str]:
    """
    Intenta obtener la foto diaria de hoy desde la SD (/photos).
    Si no la encuentra, hace una captura en vivo como fallback.

    Returns:
        (bytes de la imagen, nombre de archivo)
    """
    today_str = date.today().strftime("%Y-%m-%d")
    fallback_name = f"fotodiaria_{today_str}.jpg"

    try:
        r = requests.get(esp32_url("/photos"), timeout=REQUEST_TIMEOUT)
        r.raise_for_status()
        data = r.json()
        photos = data if isinstance(data, list) else data.get("photos", [])

        # Buscar la foto de hoy en la carpeta de fotos diarias
        for photo in photos:
            name = photo.get("name", "") if isinstance(photo, dict) else str(photo)
            if today_str in name and "diaria" in name.lower():
                photo_r = requests.get(
                    esp32_url(f"/photo?name={name}"), timeout=REQUEST_TIMEOUT
                )
                if photo_r.status_code == 200 and "image" in photo_r.headers.get(
                    "content-type", ""
                ):
                    log.info("Foto diaria encontrada en SD: %s", name)
                    return photo_r.content, name
    except Exception as exc:
        log.warning("No se pudo acceder a /photos, usando captura en vivo: %s", exc)

    # Fallback: captura en vivo
    log.info("Haciendo captura en vivo como foto diaria")
    return capture_image(), fallback_name


# ---------------------------------------------------------------------------
# Embeds de error
# ---------------------------------------------------------------------------


def error_embed(msg: str) -> discord.Embed:
    return discord.Embed(title="Error", description=msg, color=discord.Color.red())


def connection_error_embed() -> discord.Embed:
    return error_embed(
        f"No se puede conectar a la ESP32-CAM en `{ESP32_IP}:{ESP32_PORT}`.\n"
        "Verifica que la cámara esté encendida y en la misma red WiFi."
    )


# ---------------------------------------------------------------------------
# Instancia del bot
# ---------------------------------------------------------------------------

intents = discord.Intents.default()
intents.message_content = True
bot = commands.Bot(command_prefix="!", intents=intents)


@bot.event
async def on_ready() -> None:
    log.info("Bot conectado como %s (ID: %s)", bot.user, bot.user.id)
    log.info("ESP32-CAM → http://%s:%s", ESP32_IP, ESP32_PORT)
    try:
        synced = await bot.tree.sync()
        log.info("Sincronizados %d comandos slash", len(synced))
    except Exception as exc:
        log.error("Error sincronizando comandos: %s", exc)
    await bot.change_presence(
        activity=discord.Activity(
            type=discord.ActivityType.watching,
            name=f"ESP32-CAM @ {ESP32_IP}",
        )
    )


# ---------------------------------------------------------------------------
# /foto
# ---------------------------------------------------------------------------


@bot.hybrid_command(name="foto", description="Captura una imagen de la ESP32-CAM y la envía aquí")
async def cmd_foto(ctx: commands.Context) -> None:
    await ctx.defer()

    data = capture_image()
    if data is None:
        await ctx.send(embed=connection_error_embed())
        return

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = f"esp32cam_{ts}.jpg"
    embed = discord.Embed(
        title="Foto ESP32-CAM",
        description=datetime.now().strftime("Capturada el %d/%m/%Y a las %H:%M:%S"),
        color=discord.Color.green(),
    )
    embed.set_image(url=f"attachment://{filename}")
    embed.set_footer(text=f"{ESP32_IP}:{ESP32_PORT}")
    await ctx.send(embed=embed, file=discord.File(io.BytesIO(data), filename=filename))


# ---------------------------------------------------------------------------
# /foto_flash
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="foto_flash",
    description="Captura una imagen con el flash LED encendido (GPIO4)",
)
async def cmd_foto_flash(ctx: commands.Context) -> None:
    await ctx.defer()

    data = capture_image(flash=True)
    if data is None:
        await ctx.send(embed=connection_error_embed())
        return

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = f"esp32cam_flash_{ts}.jpg"
    embed = discord.Embed(
        title="Foto ESP32-CAM — Flash",
        description=datetime.now().strftime("Capturada el %d/%m/%Y a las %H:%M:%S"),
        color=discord.Color.yellow(),
    )
    embed.set_image(url=f"attachment://{filename}")
    embed.set_footer(text=f"Flash encendido  •  {ESP32_IP}:{ESP32_PORT}")
    await ctx.send(embed=embed, file=discord.File(io.BytesIO(data), filename=filename))


# ---------------------------------------------------------------------------
# /fotodiaria
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="fotodiaria",
    description="Envía la foto automática del día (busca en SD, o captura en vivo)",
)
async def cmd_fotodiaria(ctx: commands.Context) -> None:
    await ctx.defer()

    data, filename = get_daily_photo()
    if data is None:
        await ctx.send(embed=connection_error_embed())
        return

    today = date.today().strftime("%d/%m/%Y")
    from_sd = "diaria" in filename.lower() and date.today().strftime("%Y-%m-%d") in filename
    source = "Recuperada de la tarjeta SD" if from_sd else "Captura en vivo (no hay foto guardada hoy)"

    embed = discord.Embed(
        title=f"Foto diaria — {today}",
        description=source,
        color=discord.Color.orange(),
    )
    embed.set_image(url=f"attachment://{filename}")
    embed.set_footer(text=f"ESP32-CAM  •  {ESP32_IP}:{ESP32_PORT}")
    await ctx.send(embed=embed, file=discord.File(io.BytesIO(data), filename=filename))


# ---------------------------------------------------------------------------
# /video
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="video",
    description="Graba un video desde el stream MJPEG y lo envía (máx. 30 segundos)",
)
@app_commands.describe(segundos="Duración del video en segundos (1–30, default 10)")
async def cmd_video(ctx: commands.Context, segundos: int = 10) -> None:
    # Validar rango
    segundos = max(1, min(segundos, MAX_VIDEO_SECONDS))

    await ctx.defer()
    aviso = await ctx.send(
        f"Grabando {segundos} segundo{'s' if segundos > 1 else ''} de video..."
    )

    stream_url = esp32_url("/stream")
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    tmp_path = os.path.join(tempfile.gettempdir(), f"esp32cam_{ts}.mp4")

    # Grabar en un hilo separado para no bloquear el event loop de Discord
    loop = asyncio.get_running_loop()
    success = await loop.run_in_executor(
        None, record_stream, stream_url, segundos, tmp_path, None
    )

    # Borrar el mensaje de aviso
    try:
        await aviso.delete()
    except discord.HTTPException:
        pass

    if not success:
        await ctx.send(
            embed=error_embed(
                f"No se pudo grabar el video desde `{stream_url}`.\n"
                "Verifica que el stream esté activo y la cámara accesible."
            )
        )
        return

    file_size = os.path.getsize(tmp_path)
    max_size = 25 * 1024 * 1024  # 25 MB — límite de Discord sin boost

    if file_size > max_size:
        os.remove(tmp_path)
        await ctx.send(
            embed=error_embed(
                f"El video ({file_size / 1024 / 1024:.1f} MB) supera el límite de Discord (25 MB).\n"
                "Reduce la duración con `/video <segundos>`."
            )
        )
        return

    embed = discord.Embed(
        title=f"Video ESP32-CAM — {segundos}s",
        description=datetime.now().strftime("Grabado el %d/%m/%Y a las %H:%M:%S"),
        color=discord.Color.purple(),
    )
    embed.set_footer(
        text=f"{ESP32_IP}:{ESP32_PORT}  •  {file_size / 1024:.0f} KB"
    )

    with open(tmp_path, "rb") as f:
        await ctx.send(embed=embed, file=discord.File(f, filename=f"esp32cam_{ts}.mp4"))

    os.remove(tmp_path)


# ---------------------------------------------------------------------------
# /estado
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="estado",
    description="Muestra el estado del sistema: RAM, WiFi, SD y uptime",
)
async def cmd_estado(ctx: commands.Context) -> None:
    await ctx.defer()

    status = get_status()
    if status is None:
        await ctx.send(embed=connection_error_embed())
        return

    embed = discord.Embed(
        title="Estado ESP32-CAM",
        color=discord.Color.blue(),
        timestamp=datetime.utcnow(),
    )

    # RAM
    heap = status.get("heap_free") or status.get("free_heap")
    if heap is not None:
        embed.add_field(name="RAM libre", value=f"{int(heap):,} bytes", inline=True)

    psram = status.get("psram_free") or status.get("free_psram")
    if psram is not None:
        embed.add_field(name="PSRAM libre", value=f"{int(psram):,} bytes", inline=True)

    # WiFi
    rssi = status.get("wifi_rssi") or status.get("rssi")
    if rssi is not None:
        if rssi > -60:
            signal = "Excelente"
        elif rssi > -70:
            signal = "Buena"
        elif rssi > -80:
            signal = "Regular"
        else:
            signal = "Débil"
        embed.add_field(name="Señal WiFi", value=f"{rssi} dBm ({signal})", inline=True)

    ssid = status.get("wifi_ssid") or status.get("ssid")
    if ssid:
        embed.add_field(name="Red WiFi", value=ssid, inline=True)

    # Uptime
    uptime = status.get("uptime")
    if uptime is not None:
        h, rem = divmod(int(uptime), 3600)
        m, s = divmod(rem, 60)
        embed.add_field(name="Tiempo encendida", value=f"{h}h {m}m {s}s", inline=True)

    embed.add_field(name="IP", value=f"`{ESP32_IP}`", inline=False)
    embed.set_footer(text="Datos en tiempo real de la ESP32-CAM")
    await ctx.send(embed=embed)


# ---------------------------------------------------------------------------
# /ayuda
# ---------------------------------------------------------------------------


@bot.hybrid_command(name="ayuda", description="Muestra todos los comandos del bot")
async def cmd_ayuda(ctx: commands.Context) -> None:
    embed = discord.Embed(
        title="Bot ESP32-CAM — Ayuda",
        description=(
            "Controla tu ESP32-CAM desde Discord.\n"
            "Usa `/comando` (slash) o `!comando` (texto)."
        ),
        color=discord.Color.gold(),
    )
    cmds = [
        ("/foto", "Captura y envía una imagen en vivo"),
        ("/foto_flash", "Captura con el flash LED encendido"),
        ("/fotodiaria", "Foto automática del día (SD o captura en vivo)"),
        ("/video [segundos]", "Graba y envía un video (máx. 30 seg)"),
        ("/estado", "Estado del sistema: RAM, WiFi, uptime"),
        ("/ayuda", "Muestra esta ayuda"),
    ]
    for name, desc in cmds:
        embed.add_field(name=f"`{name}`", value=desc, inline=False)
    embed.set_footer(text=f"ESP32-CAM  •  http://{ESP32_IP}:{ESP32_PORT}")
    await ctx.send(embed=embed)


# ---------------------------------------------------------------------------
# Punto de entrada (llamado desde main.py)
# ---------------------------------------------------------------------------


def run() -> None:
    """Carga la configuración y arranca el bot. Llamar desde main.py."""
    _load_config()
    if not DISCORD_TOKEN:
        raise SystemExit(
            "ERROR: Falta DISCORD_TOKEN en el archivo .env\n"
            "Ejecuta la opción 'Configurar credenciales' del menú."
        )
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )
    bot.run(DISCORD_TOKEN, log_handler=None)
