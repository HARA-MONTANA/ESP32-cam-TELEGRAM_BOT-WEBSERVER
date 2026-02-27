"""
Bot de Discord para ESP32-CAM  ─  CYBERPUNK EDITION
=====================================================
Comandos disponibles (/comando o w!comando):

  /foto          — Captura y envía una imagen en vivo
  /foto_flash    — Captura con el flash LED encendido (GPIO4)
  /fotodiaria    — Envía la foto automática del día (busca en SD, o captura en vivo)
  /video [seg]   — Graba N segundos del stream y envía el .mp4 (máx. 30 s)
  /estado        — Muestra RAM, WiFi, SD y uptime de la ESP32-CAM
  /help          — Muestra esta ayuda

Configuración (archivo .env):
  DISCORD_TOKEN   — Token del bot de Discord (obligatorio)
  ESP32_IP        — IP local de la cámara  (default: 192.168.1.100)
  ESP32_PORT      — Puerto del servidor web (default: 80)
  COMMAND_PREFIX  — Prefijo para comandos de texto  (default: w!)
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
COMMAND_PREFIX: str = "w!"

MAX_VIDEO_SECONDS: int = 30
REQUEST_TIMEOUT: int = 10

# ── Paleta Cyberpunk ─────────────────────────────────────────────────────────
CYBER_GREEN  = 0x00FF9F  # #00ff9f — capturas normales
CYBER_BLUE   = 0x00B8FF  # #00b8ff — estado / info
DEEP_BLUE    = 0x001EFF  # #001eff — secundario / diaria
CYBER_PURPLE = 0xBD00FF  # #bd00ff — video / primario
NEON_PURPLE  = 0xD600FF  # #d600ff — help / acento
CYBER_RED    = 0xFF003C  # rojo neón — errores


# ── Configuración ────────────────────────────────────────────────────────────

def _load_config() -> None:
    """Carga (o recarga) las variables de entorno desde el .env."""
    global DISCORD_TOKEN, ESP32_IP, ESP32_PORT, COMMAND_PREFIX
    load_dotenv(override=True)
    DISCORD_TOKEN = os.getenv("DISCORD_TOKEN", "")
    ESP32_IP      = os.getenv("ESP32_IP", "192.168.1.100")
    ESP32_PORT    = os.getenv("ESP32_PORT", "80")
    COMMAND_PREFIX = os.getenv("COMMAND_PREFIX", "w!")


# ── Helpers HTTP → ESP32-CAM ─────────────────────────────────────────────────

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
    try:
        requests.get(esp32_url(f"/flash?state={state}"), timeout=5)
    except requests.RequestException:
        pass


def get_status() -> dict | None:
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
    """
    today_str = date.today().strftime("%Y-%m-%d")
    fallback_name = f"fotodiaria_{today_str}.jpg"
    try:
        r = requests.get(esp32_url("/photos"), timeout=REQUEST_TIMEOUT)
        r.raise_for_status()
        data = r.json()
        photos = data if isinstance(data, list) else data.get("photos", [])
        for photo in photos:
            name = photo.get("name", "") if isinstance(photo, dict) else str(photo)
            if today_str in name and "diaria" in name.lower():
                photo_r = requests.get(
                    esp32_url(f"/photo?name={name}"), timeout=REQUEST_TIMEOUT
                )
                if photo_r.status_code == 200 and "image" in photo_r.headers.get("content-type", ""):
                    log.info("Foto diaria encontrada en SD: %s", name)
                    return photo_r.content, name
    except Exception as exc:
        log.warning("No se pudo acceder a /photos, usando captura en vivo: %s", exc)
    log.info("Haciendo captura en vivo como foto diaria")
    return capture_image(), fallback_name


# ── Builders de Embeds Cyberpunk ─────────────────────────────────────────────

def _cyber_footer(extra: str = "") -> str:
    base = f"◈ ESP32-CAM  ·  {ESP32_IP}:{ESP32_PORT}"
    return f"{base}  ·  {extra}" if extra else base


def _foto_embed(filename: str, flash: bool) -> discord.Embed:
    icon  = "⚡" if flash else "📸"
    mode  = "Flash ⚡ **ON**" if flash else "Estándar 🌑"
    color = CYBER_GREEN if not flash else 0xFFFF00
    embed = discord.Embed(
        title=f"{icon}  CAPTURE  ·  ESP32-CAM",
        description=(
            f"```ansi\n\u001b[1;36m◈ SISTEMA ONLINE\u001b[0m\n```"
            f"> 🔦 **Modo:** {mode}\n"
            f"> 🕐 **Timestamp:** `{datetime.now().strftime('%Y-%m-%d  %H:%M:%S')}`"
        ),
        color=color,
    )
    embed.set_image(url=f"attachment://{filename}")
    embed.set_footer(text=_cyber_footer())
    return embed


def _fotodiaria_embed(today: str, filename: str, from_sd: bool) -> discord.Embed:
    source_icon = "💾" if from_sd else "📡"
    source_text = "Recuperada de la tarjeta SD" if from_sd else "Captura en vivo *(sin foto guardada hoy)*"
    embed = discord.Embed(
        title=f"📅  DAILY SHOT  ·  {today}",
        description=(
            f"```ansi\n\u001b[1;34m◈ FOTO DIARIA CARGADA\u001b[0m\n```"
            f"> {source_icon} **Fuente:** {source_text}\n"
            f"> 🕐 **Timestamp:** `{datetime.now().strftime('%Y-%m-%d  %H:%M:%S')}`"
        ),
        color=DEEP_BLUE,
    )
    embed.set_image(url=f"attachment://{filename}")
    embed.set_footer(text=_cyber_footer())
    return embed


def _video_embed(segundos: int, ts: str, file_size: int) -> discord.Embed:
    bars = min(10, max(1, round(segundos / MAX_VIDEO_SECONDS * 10)))
    bar_str = "█" * bars + "░" * (10 - bars)
    embed = discord.Embed(
        title=f"🎥  VIDEO REC  ·  {segundos}s",
        description=(
            f"```ansi\n\u001b[1;35m◈ GRABACIÓN COMPLETADA\u001b[0m\n```"
            f"> ⏱️ **Duración:** `{segundos}` segundos  `[{bar_str}]`\n"
            f"> 💿 **Tamaño:** `{file_size / 1024:.0f} KB`\n"
            f"> 🕐 **Timestamp:** `{datetime.now().strftime('%Y-%m-%d  %H:%M:%S')}`"
        ),
        color=CYBER_PURPLE,
    )
    embed.set_footer(text=_cyber_footer())
    return embed


def _estado_embed(status: dict) -> discord.Embed:
    embed = discord.Embed(
        title="📊  SYSTEM STATUS  ·  ESP32-CAM",
        description=f"```ansi\n\u001b[1;34m◈ DIAGNÓSTICO EN TIEMPO REAL\u001b[0m\n```",
        color=CYBER_BLUE,
        timestamp=datetime.utcnow(),
    )
    # RAM
    heap = status.get("heap_free") or status.get("free_heap")
    if heap is not None:
        embed.add_field(name="🔋 RAM Libre", value=f"`{int(heap):,}` bytes", inline=True)

    psram = status.get("psram_free") or status.get("free_psram")
    if psram is not None:
        embed.add_field(name="💾 PSRAM Libre", value=f"`{int(psram):,}` bytes", inline=True)

    embed.add_field(name="\u200b", value="\u200b", inline=True)  # spacer

    # WiFi
    rssi = status.get("wifi_rssi") or status.get("rssi")
    if rssi is not None:
        if rssi > -60:
            signal, bar = "Excelente 🟢", "▓▓▓▓▓"
        elif rssi > -70:
            signal, bar = "Buena 🟡", "▓▓▓▓░"
        elif rssi > -80:
            signal, bar = "Regular 🟠", "▓▓▓░░"
        else:
            signal, bar = "Débil 🔴", "▓░░░░"
        embed.add_field(
            name="📡 Señal WiFi",
            value=f"`{rssi} dBm`  `{bar}`\n{signal}",
            inline=True,
        )

    ssid = status.get("wifi_ssid") or status.get("ssid")
    if ssid:
        embed.add_field(name="🌐 Red WiFi", value=f"`{ssid}`", inline=True)

    embed.add_field(name="\u200b", value="\u200b", inline=True)  # spacer

    # Uptime
    uptime = status.get("uptime")
    if uptime is not None:
        h, rem = divmod(int(uptime), 3600)
        m, s = divmod(rem, 60)
        embed.add_field(name="⏱️ Uptime", value=f"`{h}h {m}m {s}s`", inline=True)

    embed.add_field(name="🔌 Dirección IP", value=f"`{ESP32_IP}`", inline=True)
    embed.set_footer(text=_cyber_footer("datos en tiempo real"))
    return embed


def error_embed(msg: str) -> discord.Embed:
    embed = discord.Embed(
        title="⛔  SYSTEM ERROR",
        description=(
            f"```ansi\n\u001b[1;31m{msg}\u001b[0m\n```"
        ),
        color=CYBER_RED,
    )
    embed.set_footer(text=_cyber_footer())
    return embed


def connection_error_embed() -> discord.Embed:
    return error_embed(
        f"Conexión rechazada → {ESP32_IP}:{ESP32_PORT}\n"
        "▸ Verifica que la cámara esté encendida\n"
        "▸ Confirma que esté en la misma red WiFi"
    )


# ── Vistas con botones ────────────────────────────────────────────────────────

class FotoView(discord.ui.View):
    """Botones interactivos para /foto."""

    def __init__(self):
        super().__init__(timeout=120)

    @discord.ui.button(label="Otra foto", emoji="📸", style=discord.ButtonStyle.primary)
    async def retake(self, interaction: discord.Interaction, button: discord.ui.Button):
        await interaction.response.defer()
        data = capture_image()
        if data is None:
            await interaction.followup.send(embed=connection_error_embed(), ephemeral=True)
            return
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        filename = f"esp32cam_{ts}.jpg"
        await interaction.followup.send(
            embed=_foto_embed(filename, flash=False),
            file=discord.File(io.BytesIO(data), filename=filename),
            view=FotoView(),
        )

    @discord.ui.button(label="Con flash", emoji="⚡", style=discord.ButtonStyle.secondary)
    async def with_flash(self, interaction: discord.Interaction, button: discord.ui.Button):
        await interaction.response.defer()
        data = capture_image(flash=True)
        if data is None:
            await interaction.followup.send(embed=connection_error_embed(), ephemeral=True)
            return
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        filename = f"esp32cam_flash_{ts}.jpg"
        await interaction.followup.send(
            embed=_foto_embed(filename, flash=True),
            file=discord.File(io.BytesIO(data), filename=filename),
            view=FotoFlashView(),
        )


class FotoFlashView(discord.ui.View):
    """Botones interactivos para /foto_flash."""

    def __init__(self):
        super().__init__(timeout=120)

    @discord.ui.button(label="Repetir flash", emoji="⚡", style=discord.ButtonStyle.primary)
    async def retake_flash(self, interaction: discord.Interaction, button: discord.ui.Button):
        await interaction.response.defer()
        data = capture_image(flash=True)
        if data is None:
            await interaction.followup.send(embed=connection_error_embed(), ephemeral=True)
            return
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        filename = f"esp32cam_flash_{ts}.jpg"
        await interaction.followup.send(
            embed=_foto_embed(filename, flash=True),
            file=discord.File(io.BytesIO(data), filename=filename),
            view=FotoFlashView(),
        )

    @discord.ui.button(label="Sin flash", emoji="📸", style=discord.ButtonStyle.secondary)
    async def no_flash(self, interaction: discord.Interaction, button: discord.ui.Button):
        await interaction.response.defer()
        data = capture_image()
        if data is None:
            await interaction.followup.send(embed=connection_error_embed(), ephemeral=True)
            return
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        filename = f"esp32cam_{ts}.jpg"
        await interaction.followup.send(
            embed=_foto_embed(filename, flash=False),
            file=discord.File(io.BytesIO(data), filename=filename),
            view=FotoView(),
        )


class FotoDiariaView(discord.ui.View):
    """Botones interactivos para /fotodiaria."""

    def __init__(self):
        super().__init__(timeout=120)

    @discord.ui.button(label="Actualizar", emoji="🔄", style=discord.ButtonStyle.primary)
    async def refresh(self, interaction: discord.Interaction, button: discord.ui.Button):
        await interaction.response.defer()
        data, filename = get_daily_photo()
        if data is None:
            await interaction.followup.send(embed=connection_error_embed(), ephemeral=True)
            return
        today = date.today().strftime("%d/%m/%Y")
        from_sd = "diaria" in filename.lower() and date.today().strftime("%Y-%m-%d") in filename
        await interaction.followup.send(
            embed=_fotodiaria_embed(today, filename, from_sd),
            file=discord.File(io.BytesIO(data), filename=filename),
            view=FotoDiariaView(),
        )

    @discord.ui.button(label="Captura en vivo", emoji="🎯", style=discord.ButtonStyle.secondary)
    async def live_capture(self, interaction: discord.Interaction, button: discord.ui.Button):
        await interaction.response.defer()
        data = capture_image()
        if data is None:
            await interaction.followup.send(embed=connection_error_embed(), ephemeral=True)
            return
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        filename = f"esp32cam_{ts}.jpg"
        await interaction.followup.send(
            embed=_foto_embed(filename, flash=False),
            file=discord.File(io.BytesIO(data), filename=filename),
            view=FotoView(),
        )


class VideoView(discord.ui.View):
    """Botones de duración rápida para /video."""

    def __init__(self):
        super().__init__(timeout=90)

    async def _record_and_send(self, interaction: discord.Interaction, segundos: int):
        await interaction.response.defer()
        bars = min(10, max(1, round(segundos / MAX_VIDEO_SECONDS * 10)))
        bar_str = "█" * bars + "░" * (10 - bars)
        msg = await interaction.followup.send(
            embed=discord.Embed(
                description=(
                    f"```ansi\n\u001b[1;35m⏺  GRABANDO...\u001b[0m\n```"
                    f"> ⏱️ **Duración:** `{segundos}s`  `[{bar_str}]`"
                ),
                color=CYBER_PURPLE,
            )
        )
        stream_url = esp32_url("/stream")
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        tmp_path = os.path.join(tempfile.gettempdir(), f"esp32cam_{ts}.mp4")
        loop = asyncio.get_running_loop()
        success = await loop.run_in_executor(None, record_stream, stream_url, segundos, tmp_path, None)
        try:
            await msg.delete()
        except discord.HTTPException:
            pass
        if not success:
            await interaction.followup.send(
                embed=error_embed(
                    f"No se pudo grabar desde el stream.\n▸ URL: {stream_url}"
                )
            )
            return
        file_size = os.path.getsize(tmp_path)
        if file_size > 25 * 1024 * 1024:
            os.remove(tmp_path)
            await interaction.followup.send(
                embed=error_embed(
                    f"Video ({file_size / 1024 / 1024:.1f} MB) supera el límite de 25 MB.\n"
                    "▸ Usa una duración menor."
                )
            )
            return
        embed = _video_embed(segundos, ts, file_size)
        with open(tmp_path, "rb") as f:
            await interaction.followup.send(
                embed=embed,
                file=discord.File(f, filename=f"esp32cam_{ts}.mp4"),
                view=VideoView(),
            )
        os.remove(tmp_path)

    @discord.ui.button(label="5 seg", emoji="⏱️", style=discord.ButtonStyle.secondary)
    async def five_sec(self, interaction: discord.Interaction, button: discord.ui.Button):
        await self._record_and_send(interaction, 5)

    @discord.ui.button(label="10 seg", emoji="⏱️", style=discord.ButtonStyle.primary)
    async def ten_sec(self, interaction: discord.Interaction, button: discord.ui.Button):
        await self._record_and_send(interaction, 10)

    @discord.ui.button(label="20 seg", emoji="⏱️", style=discord.ButtonStyle.primary)
    async def twenty_sec(self, interaction: discord.Interaction, button: discord.ui.Button):
        await self._record_and_send(interaction, 20)

    @discord.ui.button(label="30 seg", emoji="⏱️", style=discord.ButtonStyle.danger)
    async def thirty_sec(self, interaction: discord.Interaction, button: discord.ui.Button):
        await self._record_and_send(interaction, 30)


class EstadoView(discord.ui.View):
    """Botones para /estado."""

    def __init__(self):
        super().__init__(timeout=120)

    @discord.ui.button(label="Actualizar estado", emoji="🔄", style=discord.ButtonStyle.primary)
    async def refresh(self, interaction: discord.Interaction, button: discord.ui.Button):
        await interaction.response.defer()
        status = get_status()
        if status is None:
            await interaction.followup.send(embed=connection_error_embed(), ephemeral=True)
            return
        await interaction.followup.send(embed=_estado_embed(status), view=EstadoView())


# ── Instancia del bot ─────────────────────────────────────────────────────────

intents = discord.Intents.default()
intents.message_content = True


def _get_prefix(bot_instance, message) -> str:
    return COMMAND_PREFIX


bot = commands.Bot(command_prefix=_get_prefix, intents=intents)


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
            name=f"◈ ESP32-CAM @ {ESP32_IP}",
        )
    )


# ── /foto ─────────────────────────────────────────────────────────────────────

@bot.hybrid_command(name="foto", description="📸 Captura una imagen de la ESP32-CAM y la envía aquí")
async def cmd_foto(ctx: commands.Context) -> None:
    await ctx.defer()
    data = capture_image()
    if data is None:
        await ctx.send(embed=connection_error_embed())
        return
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = f"esp32cam_{ts}.jpg"
    await ctx.send(
        embed=_foto_embed(filename, flash=False),
        file=discord.File(io.BytesIO(data), filename=filename),
        view=FotoView(),
    )


# ── /foto_flash ───────────────────────────────────────────────────────────────

@bot.hybrid_command(
    name="foto_flash",
    description="⚡ Captura una imagen con el flash LED encendido (GPIO4)",
)
async def cmd_foto_flash(ctx: commands.Context) -> None:
    await ctx.defer()
    data = capture_image(flash=True)
    if data is None:
        await ctx.send(embed=connection_error_embed())
        return
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = f"esp32cam_flash_{ts}.jpg"
    await ctx.send(
        embed=_foto_embed(filename, flash=True),
        file=discord.File(io.BytesIO(data), filename=filename),
        view=FotoFlashView(),
    )


# ── /fotodiaria ───────────────────────────────────────────────────────────────

@bot.hybrid_command(
    name="fotodiaria",
    description="📅 Envía la foto automática del día (busca en SD, o captura en vivo)",
)
async def cmd_fotodiaria(ctx: commands.Context) -> None:
    await ctx.defer()
    data, filename = get_daily_photo()
    if data is None:
        await ctx.send(embed=connection_error_embed())
        return
    today = date.today().strftime("%d/%m/%Y")
    from_sd = "diaria" in filename.lower() and date.today().strftime("%Y-%m-%d") in filename
    await ctx.send(
        embed=_fotodiaria_embed(today, filename, from_sd),
        file=discord.File(io.BytesIO(data), filename=filename),
        view=FotoDiariaView(),
    )


# ── /video ────────────────────────────────────────────────────────────────────

@bot.hybrid_command(
    name="video",
    description="🎥 Graba un video desde el stream MJPEG y lo envía (máx. 30 segundos)",
)
@app_commands.describe(segundos="Duración del video en segundos (1–30, default 10)")
async def cmd_video(ctx: commands.Context, segundos: int = 10) -> None:
    segundos = max(1, min(segundos, MAX_VIDEO_SECONDS))
    await ctx.defer()

    bars = min(10, max(1, round(segundos / MAX_VIDEO_SECONDS * 10)))
    bar_str = "█" * bars + "░" * (10 - bars)
    aviso = await ctx.send(
        embed=discord.Embed(
            description=(
                f"```ansi\n\u001b[1;35m⏺  GRABANDO...\u001b[0m\n```"
                f"> ⏱️ **Duración:** `{segundos}s`  `[{bar_str}]`"
            ),
            color=CYBER_PURPLE,
        )
    )

    stream_url = esp32_url("/stream")
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    tmp_path = os.path.join(tempfile.gettempdir(), f"esp32cam_{ts}.mp4")
    loop = asyncio.get_running_loop()
    success = await loop.run_in_executor(None, record_stream, stream_url, segundos, tmp_path, None)

    try:
        await aviso.delete()
    except discord.HTTPException:
        pass

    if not success:
        await ctx.send(
            embed=error_embed(
                f"No se pudo grabar el video desde {stream_url}.\n"
                "▸ Verifica que el stream esté activo y la cámara accesible."
            )
        )
        return

    file_size = os.path.getsize(tmp_path)
    if file_size > 25 * 1024 * 1024:
        os.remove(tmp_path)
        await ctx.send(
            embed=error_embed(
                f"Video ({file_size / 1024 / 1024:.1f} MB) supera el límite de Discord (25 MB).\n"
                "▸ Reduce la duración con `/video <segundos>`."
            )
        )
        return

    embed = _video_embed(segundos, ts, file_size)
    with open(tmp_path, "rb") as f:
        await ctx.send(
            embed=embed,
            file=discord.File(f, filename=f"esp32cam_{ts}.mp4"),
            view=VideoView(),
        )
    os.remove(tmp_path)


# ── /estado ───────────────────────────────────────────────────────────────────

@bot.hybrid_command(
    name="estado",
    description="📊 Muestra el estado del sistema: RAM, WiFi, SD y uptime",
)
async def cmd_estado(ctx: commands.Context) -> None:
    await ctx.defer()
    status = get_status()
    if status is None:
        await ctx.send(embed=connection_error_embed())
        return
    await ctx.send(embed=_estado_embed(status), view=EstadoView())


# ── /help ─────────────────────────────────────────────────────────────────────

@bot.hybrid_command(name="help", description="❓ Muestra todos los comandos del bot")
async def cmd_help(ctx: commands.Context) -> None:
    prefix = COMMAND_PREFIX
    embed = discord.Embed(
        title="◈  CYBER VISION  ·  ESP32-CAM BOT",
        description=(
            "```\n"
            "╔══════════════════════════════════╗\n"
            "║  ◈  C Y B E R   V I S I O N  ◈  ║\n"
            "║       E S P 3 2 - C A M          ║\n"
            "╚══════════════════════════════════╝\n"
            "```\n"
            f"> Controla tu ESP32-CAM desde Discord.\n"
            f"> Usa `/comando` *(slash)* o `{prefix}comando` *(texto)*."
        ),
        color=NEON_PURPLE,
    )
    cmds = [
        ("📸  `/foto`",              "Captura y envía una imagen en vivo"),
        ("⚡  `/foto_flash`",        "Captura con el flash LED encendido"),
        ("📅  `/fotodiaria`",        "Foto automática del día *(SD o captura en vivo)*"),
        ("🎥  `/video [segundos]`",  "Graba y envía un video *(máx. 30 seg)*"),
        ("📊  `/estado`",            "Estado del sistema: RAM, WiFi, uptime"),
        ("❓  `/help`",              "Muestra esta ayuda"),
    ]
    for name, desc in cmds:
        embed.add_field(name=name, value=f"> {desc}", inline=False)
    embed.set_footer(text=_cyber_footer(f"prefix: {prefix}"))
    await ctx.send(embed=embed)


# ── Punto de entrada ──────────────────────────────────────────────────────────

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
