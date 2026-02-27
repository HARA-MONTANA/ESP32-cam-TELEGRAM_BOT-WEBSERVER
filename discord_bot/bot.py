"""
Discord Bot para ESP32-CAM
==========================
Bot de Discord que se conecta a la ESP32-CAM vía HTTP para capturar
imágenes y enviarlas a un canal de Discord.

Comandos disponibles (slash / y prefijo !):
  /foto            - Captura una imagen y la envía al chat
  /foto_flash      - Captura con flash activado
  /guardar         - Captura y guarda en la tarjeta SD de la cámara
  /estado          - Muestra el estado del sistema (RAM, WiFi, SD)
  /stream          - Muestra la URL del stream de video en vivo
  /flash on|off    - Enciende o apaga el flash LED (GPIO4)
  /fotos           - Lista las fotos guardadas en la SD
  /ip              - Muestra la IP configurada de la ESP32-CAM
  /configurar      - Cambia la IP/puerto de la ESP32-CAM (solo admins)
  /ayuda           - Muestra esta ayuda

Configuración (.env):
  DISCORD_TOKEN    - Token del bot de Discord (obligatorio)
  ESP32_IP         - IP local de la ESP32-CAM (default: 192.168.1.100)
  ESP32_PORT       - Puerto del servidor web (default: 80)
  COMMAND_PREFIX   - Prefijo para comandos de texto (default: !)
"""

import os
import io
import json
import logging
from datetime import datetime

import discord
from discord import app_commands
from discord.ext import commands
import requests
from dotenv import load_dotenv

# ---------------------------------------------------------------------------
# Configuración inicial
# ---------------------------------------------------------------------------

load_dotenv()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("esp32-discord-bot")

DISCORD_TOKEN: str = os.getenv("DISCORD_TOKEN", "")
ESP32_IP: str = os.getenv("ESP32_IP", "192.168.1.100")
ESP32_PORT: str = os.getenv("ESP32_PORT", "80")
COMMAND_PREFIX: str = os.getenv("COMMAND_PREFIX", "!")
REQUEST_TIMEOUT: int = 10  # segundos

if not DISCORD_TOKEN:
    raise SystemExit(
        "ERROR: Falta DISCORD_TOKEN en el archivo .env\n"
        "Copia .env.example a .env y completa el token."
    )

# ---------------------------------------------------------------------------
# Helpers de conexión a la ESP32-CAM
# ---------------------------------------------------------------------------


def esp32_url(path: str = "") -> str:
    """Devuelve la URL base de la ESP32-CAM con la ruta indicada."""
    return f"http://{ESP32_IP}:{ESP32_PORT}{path}"


def capture_image(flash: bool = False) -> bytes | None:
    """
    Captura una imagen de la ESP32-CAM.

    Primero activa el flash si se solicita, luego llama al endpoint /capture
    y finalmente apaga el flash.

    Returns:
        bytes con el JPEG capturado, o None si hay error.
    """
    if flash:
        _set_flash("on")

    try:
        response = requests.get(esp32_url("/capture"), timeout=REQUEST_TIMEOUT)
        response.raise_for_status()
        content_type = response.headers.get("content-type", "")
        if "image" not in content_type:
            log.warning("Respuesta inesperada de /capture: %s", content_type)
            return None
        return response.content
    except requests.RequestException as exc:
        log.error("Error capturando imagen: %s", exc)
        return None
    finally:
        if flash:
            _set_flash("off")


def _set_flash(state: str) -> bool:
    """
    Envía el comando de flash a la ESP32-CAM.

    La cámara expone /flash?state=on|off desde web_server.cpp.
    Devuelve True si tuvo éxito.
    """
    try:
        response = requests.get(
            esp32_url(f"/flash?state={state}"), timeout=5
        )
        return response.status_code == 200
    except requests.RequestException as exc:
        log.warning("No se pudo controlar el flash (%s): %s", state, exc)
        return False


def get_status() -> dict | None:
    """Obtiene el estado del sistema desde /status."""
    try:
        response = requests.get(esp32_url("/status"), timeout=REQUEST_TIMEOUT)
        response.raise_for_status()
        return response.json()
    except requests.RequestException as exc:
        log.error("Error obteniendo estado: %s", exc)
        return None
    except json.JSONDecodeError:
        log.error("Respuesta de /status no es JSON válido")
        return None


def save_to_sd() -> dict | None:
    """Captura y guarda en la SD usando /web-capture."""
    try:
        response = requests.get(esp32_url("/web-capture"), timeout=REQUEST_TIMEOUT)
        response.raise_for_status()
        # La respuesta puede ser JSON con info del archivo guardado
        try:
            return response.json()
        except json.JSONDecodeError:
            return {"ok": True}
    except requests.RequestException as exc:
        log.error("Error guardando en SD: %s", exc)
        return None


def list_photos() -> list[dict] | None:
    """Lista las fotos guardadas en la SD desde /photos."""
    try:
        response = requests.get(esp32_url("/photos"), timeout=REQUEST_TIMEOUT)
        response.raise_for_status()
        data = response.json()
        # El endpoint devuelve lista de fotos o un objeto con clave "photos"
        if isinstance(data, list):
            return data
        if isinstance(data, dict) and "photos" in data:
            return data["photos"]
        return []
    except requests.RequestException as exc:
        log.error("Error listando fotos: %s", exc)
        return None
    except json.JSONDecodeError:
        log.error("Respuesta de /photos no es JSON válido")
        return None


# ---------------------------------------------------------------------------
# Embeds reutilizables
# ---------------------------------------------------------------------------


def error_embed(message: str) -> discord.Embed:
    return discord.Embed(
        title="Error",
        description=message,
        color=discord.Color.red(),
    )


def connection_error_embed() -> discord.Embed:
    return error_embed(
        f"No se puede conectar a la ESP32-CAM en `{ESP32_IP}:{ESP32_PORT}`.\n"
        "Verifica que la cámara esté encendida y en la misma red."
    )


# ---------------------------------------------------------------------------
# Bot
# ---------------------------------------------------------------------------

intents = discord.Intents.default()
intents.message_content = True

bot = commands.Bot(command_prefix=COMMAND_PREFIX, intents=intents)


@bot.event
async def on_ready() -> None:
    log.info("Bot conectado como %s (ID: %s)", bot.user, bot.user.id)
    log.info("ESP32-CAM configurada en http://%s:%s", ESP32_IP, ESP32_PORT)
    try:
        synced = await bot.tree.sync()
        log.info("Sincronizados %d comandos slash", len(synced))
    except Exception as exc:
        log.error("Error sincronizando comandos slash: %s", exc)

    await bot.change_presence(
        activity=discord.Activity(
            type=discord.ActivityType.watching,
            name=f"ESP32-CAM @ {ESP32_IP}",
        )
    )


# ---------------------------------------------------------------------------
# Comando: /foto
# ---------------------------------------------------------------------------


@bot.hybrid_command(name="foto", description="Captura una imagen de la ESP32-CAM y la envía aquí")
async def cmd_foto(ctx: commands.Context) -> None:
    await ctx.defer()

    image_data = capture_image(flash=False)
    if image_data is None:
        await ctx.send(embed=connection_error_embed())
        return

    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = f"esp32cam_{timestamp}.jpg"

    embed = discord.Embed(
        title="Foto ESP32-CAM",
        description=datetime.now().strftime("Capturada el %d/%m/%Y a las %H:%M:%S"),
        color=discord.Color.green(),
    )
    embed.set_image(url=f"attachment://{filename}")
    embed.set_footer(text=f"ESP32-CAM  •  {ESP32_IP}:{ESP32_PORT}")

    await ctx.send(
        embed=embed,
        file=discord.File(io.BytesIO(image_data), filename=filename),
    )


# ---------------------------------------------------------------------------
# Comando: /foto_flash
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="foto_flash",
    description="Captura una imagen con el flash LED activado",
)
async def cmd_foto_flash(ctx: commands.Context) -> None:
    await ctx.defer()

    image_data = capture_image(flash=True)
    if image_data is None:
        await ctx.send(embed=connection_error_embed())
        return

    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = f"esp32cam_flash_{timestamp}.jpg"

    embed = discord.Embed(
        title="Foto ESP32-CAM (con flash)",
        description=datetime.now().strftime("Capturada el %d/%m/%Y a las %H:%M:%S"),
        color=discord.Color.yellow(),
    )
    embed.set_image(url=f"attachment://{filename}")
    embed.set_footer(text=f"Flash activado  •  {ESP32_IP}:{ESP32_PORT}")

    await ctx.send(
        embed=embed,
        file=discord.File(io.BytesIO(image_data), filename=filename),
    )


# ---------------------------------------------------------------------------
# Comando: /guardar
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="guardar",
    description="Captura una imagen y la guarda en la tarjeta SD de la cámara",
)
async def cmd_guardar(ctx: commands.Context) -> None:
    await ctx.defer()

    result = save_to_sd()
    if result is None:
        await ctx.send(embed=connection_error_embed())
        return

    embed = discord.Embed(
        title="Foto guardada en SD",
        color=discord.Color.blurple(),
    )
    embed.add_field(
        name="Timestamp",
        value=datetime.now().strftime("%d/%m/%Y %H:%M:%S"),
        inline=False,
    )
    if isinstance(result, dict) and "filename" in result:
        embed.add_field(name="Archivo", value=result["filename"], inline=False)
    embed.set_footer(text=f"ESP32-CAM  •  {ESP32_IP}:{ESP32_PORT}")

    await ctx.send(embed=embed)


# ---------------------------------------------------------------------------
# Comando: /estado
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="estado",
    description="Muestra el estado del sistema de la ESP32-CAM (RAM, WiFi, SD)",
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
        signal = (
            "Excelente" if rssi > -60
            else "Buena" if rssi > -70
            else "Regular" if rssi > -80
            else "Débil"
        )
        embed.add_field(
            name="Señal WiFi", value=f"{rssi} dBm ({signal})", inline=True
        )

    ssid = status.get("wifi_ssid") or status.get("ssid")
    if ssid:
        embed.add_field(name="Red WiFi", value=ssid, inline=True)

    # SD Card
    sd_total = status.get("sd_total")
    sd_used = status.get("sd_used")
    sd_free = status.get("sd_free")
    if sd_total is not None:
        embed.add_field(name="SD Total", value=f"{sd_total} MB", inline=True)
    if sd_used is not None:
        embed.add_field(name="SD Usada", value=f"{sd_used} MB", inline=True)
    if sd_free is not None:
        embed.add_field(name="SD Libre", value=f"{sd_free} MB", inline=True)

    # Uptime / hora
    uptime = status.get("uptime")
    if uptime is not None:
        hours, rem = divmod(int(uptime), 3600)
        mins, secs = divmod(rem, 60)
        embed.add_field(
            name="Tiempo encendida",
            value=f"{hours}h {mins}m {secs}s",
            inline=True,
        )

    embed.add_field(name="IP", value=f"`{ESP32_IP}`", inline=False)
    embed.set_footer(text="Datos en tiempo real de la ESP32-CAM")

    await ctx.send(embed=embed)


# ---------------------------------------------------------------------------
# Comando: /stream
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="stream",
    description="Muestra la URL del stream de video en vivo de la ESP32-CAM",
)
async def cmd_stream(ctx: commands.Context) -> None:
    stream_url = esp32_url("/stream")
    dashboard_url = esp32_url("/")

    embed = discord.Embed(
        title="Stream de video — ESP32-CAM",
        description="Abre la URL en tu navegador para ver el video en vivo (MJPEG).",
        color=discord.Color.red(),
    )
    embed.add_field(name="Stream directo", value=f"`{stream_url}`", inline=False)
    embed.add_field(name="Panel de control", value=f"`{dashboard_url}`", inline=False)
    embed.add_field(
        name="Nota",
        value=(
            "Discord no puede mostrar streams MJPEG directamente.\n"
            "Usa el comando `/foto` para recibir imágenes individuales."
        ),
        inline=False,
    )
    embed.set_footer(text=f"ESP32-CAM  •  {ESP32_IP}:{ESP32_PORT}")

    await ctx.send(embed=embed)


# ---------------------------------------------------------------------------
# Comando: /flash
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="flash",
    description="Enciende o apaga el flash LED de la ESP32-CAM (GPIO4)",
)
@app_commands.describe(estado="on para encender, off para apagar")
@app_commands.choices(
    estado=[
        app_commands.Choice(name="Encender", value="on"),
        app_commands.Choice(name="Apagar", value="off"),
    ]
)
async def cmd_flash(ctx: commands.Context, estado: str) -> None:
    await ctx.defer()

    success = _set_flash(estado)

    if success:
        emoji = "💡" if estado == "on" else "🌑"
        embed = discord.Embed(
            title=f"{emoji} Flash {estado.upper()}",
            description=f"El flash LED ha sido {'encendido' if estado == 'on' else 'apagado'}.",
            color=discord.Color.yellow() if estado == "on" else discord.Color.dark_gray(),
        )
    else:
        embed = error_embed(
            f"No se pudo {'encender' if estado == 'on' else 'apagar'} el flash.\n"
            "Verifica que la ESP32-CAM esté conectada y que el endpoint `/flash` esté disponible."
        )

    await ctx.send(embed=embed)


# ---------------------------------------------------------------------------
# Comando: /fotos
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="fotos",
    description="Lista las últimas fotos guardadas en la tarjeta SD",
)
async def cmd_fotos(ctx: commands.Context) -> None:
    await ctx.defer()

    photos = list_photos()
    if photos is None:
        await ctx.send(embed=connection_error_embed())
        return

    if not photos:
        await ctx.send(
            embed=discord.Embed(
                title="Fotos en SD",
                description="No hay fotos guardadas en la tarjeta SD.",
                color=discord.Color.light_gray(),
            )
        )
        return

    # Mostrar máximo 15 fotos para no saturar el embed
    shown = photos[:15]
    lines = []
    for i, photo in enumerate(shown, start=1):
        name = photo.get("name") or photo.get("filename") or str(photo)
        size = photo.get("size")
        line = f"`{i:02d}.` {name}"
        if size:
            line += f"  ({size})"
        lines.append(line)

    embed = discord.Embed(
        title=f"Fotos en SD ({len(photos)} total)",
        description="\n".join(lines),
        color=discord.Color.blurple(),
    )
    if len(photos) > 15:
        embed.set_footer(text=f"Mostrando las últimas 15 de {len(photos)} fotos.")

    await ctx.send(embed=embed)


# ---------------------------------------------------------------------------
# Comando: /ip
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="ip",
    description="Muestra la IP y puerto configurados de la ESP32-CAM",
)
async def cmd_ip(ctx: commands.Context) -> None:
    embed = discord.Embed(
        title="Dirección ESP32-CAM",
        color=discord.Color.teal(),
    )
    embed.add_field(name="IP", value=f"`{ESP32_IP}`", inline=True)
    embed.add_field(name="Puerto", value=f"`{ESP32_PORT}`", inline=True)
    embed.add_field(
        name="URL base",
        value=f"`http://{ESP32_IP}:{ESP32_PORT}`",
        inline=False,
    )
    embed.set_footer(text="Usa /configurar para cambiar la IP (solo administradores).")
    await ctx.send(embed=embed)


# ---------------------------------------------------------------------------
# Comando: /configurar  (solo administradores del servidor)
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="configurar",
    description="Cambia la IP y puerto de la ESP32-CAM (solo administradores)",
)
@app_commands.describe(
    ip="Nueva dirección IP de la ESP32-CAM (ej. 192.168.1.50)",
    puerto="Puerto del servidor web (por defecto 80)",
)
@commands.has_permissions(administrator=True)
async def cmd_configurar(
    ctx: commands.Context, ip: str, puerto: int = 80
) -> None:
    global ESP32_IP, ESP32_PORT
    old_ip, old_port = ESP32_IP, ESP32_PORT
    ESP32_IP = ip
    ESP32_PORT = str(puerto)

    embed = discord.Embed(
        title="Configuración actualizada",
        color=discord.Color.green(),
    )
    embed.add_field(name="Anterior", value=f"`{old_ip}:{old_port}`", inline=True)
    embed.add_field(name="Nueva", value=f"`{ESP32_IP}:{ESP32_PORT}`", inline=True)
    embed.set_footer(
        text="Nota: Este cambio es temporal. Para hacerlo permanente, edita el .env."
    )
    await ctx.send(embed=embed)

    # Actualizar estado del bot
    await bot.change_presence(
        activity=discord.Activity(
            type=discord.ActivityType.watching,
            name=f"ESP32-CAM @ {ESP32_IP}",
        )
    )


@cmd_configurar.error
async def configurar_error(ctx: commands.Context, error: Exception) -> None:
    if isinstance(error, commands.MissingPermissions):
        await ctx.send(
            embed=error_embed("Solo los administradores del servidor pueden usar este comando.")
        )


# ---------------------------------------------------------------------------
# Comando: /ayuda
# ---------------------------------------------------------------------------


@bot.hybrid_command(
    name="ayuda",
    description="Muestra todos los comandos disponibles del bot",
)
async def cmd_ayuda(ctx: commands.Context) -> None:
    embed = discord.Embed(
        title="Bot ESP32-CAM para Discord",
        description=(
            "Controla tu cámara ESP32-CAM directamente desde Discord.\n"
            f"Puedes usar `/comando` o `{COMMAND_PREFIX}comando`."
        ),
        color=discord.Color.gold(),
    )

    cmds = [
        ("/foto", "Captura una imagen y la envía al chat"),
        ("/foto_flash", "Captura una imagen con el flash encendido"),
        ("/guardar", "Captura y guarda la imagen en la tarjeta SD"),
        ("/estado", "Estado del sistema: RAM, WiFi, SD"),
        ("/stream", "URL del stream de video en vivo"),
        ("/flash on|off", "Enciende o apaga el flash LED"),
        ("/fotos", "Lista las fotos guardadas en la SD"),
        ("/ip", "Muestra la IP configurada de la cámara"),
        ("/configurar IP [puerto]", "Cambia la IP de la cámara (admin)"),
        ("/ayuda", "Muestra este menú"),
    ]

    for name, desc in cmds:
        embed.add_field(name=f"`{name}`", value=desc, inline=False)

    embed.set_footer(text=f"ESP32-CAM conectada en http://{ESP32_IP}:{ESP32_PORT}")
    await ctx.send(embed=embed)


# ---------------------------------------------------------------------------
# Punto de entrada
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    bot.run(DISCORD_TOKEN, log_handler=None)
