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
from dotenv import load_dotenv, set_key
from pathlib import Path

from recorder import record_stream

log = logging.getLogger("esp32-discord-bot")

# Valores de configuración — se rellenan en run() con _load_config()
DISCORD_TOKEN: str = ""
ESP32_IP: str = "192.168.1.100"
ESP32_PORT: str = "80"
COMMAND_PREFIX: str = "w!"

MAX_VIDEO_SECONDS: int = 30
REQUEST_TIMEOUT: int = 10
REQUIRED_ROLE_ID: int = 0  # 0 = sin restricción, cualquiera puede usar el bot

# ── Paleta Cyberpunk ─────────────────────────────────────────────────────────
CYBER_GREEN  = 0x00FF9F  # #00ff9f — reservado
CYBER_BLUE   = 0x00B8FF  # #00b8ff — video / grabación / estado
DEEP_BLUE    = 0x001EFF  # #001eff — secundario
CYBER_PURPLE = 0xBD00FF  # #bd00ff — fotos (foto / fotodiaria)
NEON_PURPLE  = 0xD600FF  # #d600ff — foto con flash / help / acento
CYBER_RED    = 0xFF003C  # rojo neón — errores


# ── Configuración ────────────────────────────────────────────────────────────

_ENV_PATH = Path(__file__).parent / ".env"


def _save_env(key: str, value: str) -> None:
    """Persiste un par clave=valor en el archivo .env."""
    set_key(str(_ENV_PATH), key, value)


def _load_config() -> None:
    """Carga (o recarga) las variables de entorno desde el .env."""
    global DISCORD_TOKEN, ESP32_IP, ESP32_PORT, COMMAND_PREFIX, REQUIRED_ROLE_ID
    load_dotenv(override=True)
    DISCORD_TOKEN    = os.getenv("DISCORD_TOKEN", "")
    ESP32_IP         = os.getenv("ESP32_IP", "192.168.1.100")
    ESP32_PORT       = os.getenv("ESP32_PORT", "80")
    COMMAND_PREFIX   = os.getenv("COMMAND_PREFIX", "w!")
    REQUIRED_ROLE_ID = int(os.getenv("REQUIRED_ROLE_ID", "0"))


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


def list_sd_files() -> list | None:
    """Devuelve la lista de archivos en la SD desde /photos."""
    try:
        r = requests.get(esp32_url("/photos"), timeout=REQUEST_TIMEOUT)
        r.raise_for_status()
        data = r.json()
        return data if isinstance(data, list) else data.get("photos", [])
    except Exception as exc:
        log.error("Error listando archivos SD: %s", exc)
        return None


def get_sd_file(name: str) -> bytes | None:
    """Descarga un archivo específico de la SD por nombre/ruta."""
    try:
        r = requests.get(esp32_url(f"/photo?name={name}"), timeout=REQUEST_TIMEOUT)
        r.raise_for_status()
        return r.content
    except Exception as exc:
        log.error("Error descargando archivo SD '%s': %s", name, exc)
        return None


# ── Builders de Embeds Cyberpunk ─────────────────────────────────────────────

def _cyber_footer(extra: str = "") -> str:
    base = f"◈ ESP32-CAM  ·  {ESP32_IP}:{ESP32_PORT}"
    return f"{base}  ·  {extra}" if extra else base


def _foto_embed(filename: str, flash: bool) -> discord.Embed:
    icon  = "⚡" if flash else "📸"
    mode  = "Flash ⚡ **ON**" if flash else "Estándar 🌑"
    color = CYBER_PURPLE if not flash else NEON_PURPLE
    embed = discord.Embed(
        title=f"{icon}  CAPTURE  ·  ESP32-CAM",
        description=(
            f"```ansi\n\u001b[1;35m◈ SISTEMA ONLINE\u001b[0m\n```"
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
            f"```ansi\n\u001b[1;35m◈ FOTO DIARIA CARGADA\u001b[0m\n```"
            f"> {source_icon} **Fuente:** {source_text}\n"
            f"> 🕐 **Timestamp:** `{datetime.now().strftime('%Y-%m-%d  %H:%M:%S')}`"
        ),
        color=CYBER_PURPLE,
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
            f"```ansi\n\u001b[1;34m◈ GRABACIÓN COMPLETADA\u001b[0m\n```"
            f"> ⏱️ **Duración:** `{segundos}` segundos  `[{bar_str}]`\n"
            f"> 💿 **Tamaño:** `{file_size / 1024:.0f} KB`\n"
            f"> 🕐 **Timestamp:** `{datetime.now().strftime('%Y-%m-%d  %H:%M:%S')}`"
        ),
        color=CYBER_BLUE,
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


_SD_FILES_PER_PAGE = 20


def _sd_embed(files: list, page: int, total_pages: int) -> discord.Embed:
    start = page * _SD_FILES_PER_PAGE
    page_files = files[start:start + _SD_FILES_PER_PAGE]
    lines = []
    for i, f in enumerate(page_files, start=start + 1):
        name = f.get("name", "") if isinstance(f, dict) else str(f)
        size = f.get("size", 0) if isinstance(f, dict) else 0
        basename = name.rsplit("/", 1)[-1] or name
        if basename.lower().endswith((".jpg", ".jpeg", ".png")):
            icon = "🖼️"
        elif basename.lower().endswith((".mp4", ".avi", ".mov")):
            icon = "🎥"
        else:
            icon = "📄"
        size_str = f"`{size / 1024:.1f} KB`" if size else "`? KB`"
        lines.append(f"`{i:02d}.` {icon} `{basename}` — {size_str}")
    desc = "\n".join(lines) if lines else "> *No hay archivos en la SD*"
    count = len(files)
    embed = discord.Embed(
        title="💾  SD CARD BROWSER  ·  ESP32-CAM",
        description=(
            f"```ansi\n\u001b[1;35m◈ {count} ARCHIVO{'S' if count != 1 else ''} ENCONTRADO{'S' if count != 1 else ''}\u001b[0m\n```"
            f"{desc}"
        ),
        color=CYBER_PURPLE,
    )
    footer_extra = f"Página {page + 1} / {total_pages}" if total_pages > 1 else ""
    embed.set_footer(text=_cyber_footer(footer_extra) if footer_extra else _cyber_footer())
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


def role_denied_embed() -> discord.Embed:
    role_mention = f"<@&{REQUIRED_ROLE_ID}>" if REQUIRED_ROLE_ID else "rol requerido"
    embed = discord.Embed(
        title="🔒  ACCESO DENEGADO",
        description=(
            "```ansi\n\u001b[1;31m◈ PERMISOS INSUFICIENTES\u001b[0m\n```"
            f"> No tienes el rol necesario para usar este bot.\n"
            f"> 🎭 **Rol requerido:** {role_mention}"
        ),
        color=CYBER_RED,
    )
    embed.set_footer(text=_cyber_footer())
    return embed


def _deactivate_role() -> discord.Embed:
    """Pone REQUIRED_ROLE_ID a 0, guarda en .env y devuelve el embed de confirmación."""
    global REQUIRED_ROLE_ID
    REQUIRED_ROLE_ID = 0
    _save_env("REQUIRED_ROLE_ID", "0")
    embed = discord.Embed(
        title="🔓  ROL REQUERIDO  ·  Desactivado",
        description=(
            "```ansi\n\u001b[1;34m◈ RESTRICCIÓN ELIMINADA\u001b[0m\n```"
            "> El bot ahora está **abierto a todos** los miembros del servidor."
        ),
        color=CYBER_BLUE,
    )
    embed.set_footer(text=_cyber_footer())
    return embed


# ── Árbol slash con verificación de rol ──────────────────────────────────────

class CyberTree(app_commands.CommandTree):
    async def interaction_check(self, interaction: discord.Interaction) -> bool:
        if REQUIRED_ROLE_ID == 0:
            return True
        member = interaction.user
        if isinstance(member, discord.Member) and member.guild_permissions.administrator:
            return True
        if interaction.guild is None:
            await interaction.response.send_message(
                embed=role_denied_embed(), ephemeral=True
            )
            return False
        role = interaction.guild.get_role(REQUIRED_ROLE_ID)
        if role is None or role in member.roles:
            return True
        await interaction.response.send_message(
            embed=role_denied_embed(), ephemeral=True
        )
        return False


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
                    f"```ansi\n\u001b[1;34m⏺  GRABANDO...\u001b[0m\n```"
                    f"> ⏱️ **Duración:** `{segundos}s`  `[{bar_str}]`"
                ),
                color=CYBER_BLUE,
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


# ── SD Card Views ────────────────────────────────────────────────────────────

class FileSelectMenu(discord.ui.Select):
    """Menú desplegable con los archivos de la página actual de la SD."""

    def __init__(self, files_page: list):
        options = []
        for f in files_page:
            name = f.get("name", "") if isinstance(f, dict) else str(f)
            size = f.get("size", 0) if isinstance(f, dict) else 0
            basename = name.rsplit("/", 1)[-1] or name
            if basename.lower().endswith((".jpg", ".jpeg", ".png")):
                icon = "🖼️"
            elif basename.lower().endswith((".mp4", ".avi", ".mov")):
                icon = "🎥"
            else:
                icon = "📄"
            label = basename[:100] or "archivo"
            desc  = f"{size / 1024:.1f} KB" if size else None
            options.append(discord.SelectOption(
                label=label,
                value=name[:100],
                description=desc,
                emoji=icon,
            ))
        super().__init__(
            placeholder="📂 Selecciona un archivo para enviarlo...",
            options=options,
            min_values=1,
            max_values=1,
        )

    async def callback(self, interaction: discord.Interaction):
        name = self.values[0]
        await interaction.response.defer()
        loop = asyncio.get_running_loop()
        data = await loop.run_in_executor(None, get_sd_file, name)
        if data is None:
            await interaction.followup.send(
                embed=error_embed(f"No se pudo descargar `{name}` de la SD."),
                ephemeral=True,
            )
            return
        basename = name.rsplit("/", 1)[-1] or name
        embed = discord.Embed(
            title=f"💾  SD FILE  ·  {basename}",
            description=(
                f"```ansi\n\u001b[1;35m◈ ARCHIVO ENVIADO\u001b[0m\n```"
                f"> 📂 **Ruta:** `{name}`\n"
                f"> 💿 **Tamaño:** `{len(data) / 1024:.1f} KB`"
            ),
            color=CYBER_PURPLE,
        )
        if basename.lower().endswith((".jpg", ".jpeg", ".png")):
            embed.set_image(url=f"attachment://{basename}")
        embed.set_footer(text=_cyber_footer())
        await interaction.followup.send(
            embed=embed,
            file=discord.File(io.BytesIO(data), filename=basename),
        )


class SDView(discord.ui.View):
    """Vista paginada para explorar archivos de la SD."""

    def __init__(self, files: list, page: int = 0):
        super().__init__(timeout=180)
        self.files = files
        self.page  = page
        self.total_pages = max(1, (len(files) + _SD_FILES_PER_PAGE - 1) // _SD_FILES_PER_PAGE)
        self._rebuild()

    def _page_slice(self) -> list:
        start = self.page * _SD_FILES_PER_PAGE
        return self.files[start:start + _SD_FILES_PER_PAGE]

    def _rebuild(self):
        self.clear_items()
        page_files = self._page_slice()
        if page_files:
            self.add_item(FileSelectMenu(page_files))
        if self.page > 0:
            prev = discord.ui.Button(
                label=f"◀  Pág. {self.page}",
                style=discord.ButtonStyle.secondary,
                row=1,
            )
            prev.callback = self._go_prev
            self.add_item(prev)
        if self.page < self.total_pages - 1:
            nxt = discord.ui.Button(
                label=f"Pág. {self.page + 2}  ▶",
                style=discord.ButtonStyle.secondary,
                row=1,
            )
            nxt.callback = self._go_next
            self.add_item(nxt)

    async def _go_prev(self, interaction: discord.Interaction):
        self.page -= 1
        self._rebuild()
        await interaction.response.edit_message(
            embed=_sd_embed(self.files, self.page, self.total_pages),
            view=self,
        )

    async def _go_next(self, interaction: discord.Interaction):
        self.page += 1
        self._rebuild()
        await interaction.response.edit_message(
            embed=_sd_embed(self.files, self.page, self.total_pages),
            view=self,
        )


class RolView(discord.ui.View):
    """Botón para desactivar la restricción de rol desde el embed de estado."""

    def __init__(self):
        super().__init__(timeout=60)

    @discord.ui.button(label="Desactivar restricción", emoji="🔓", style=discord.ButtonStyle.danger)
    async def deactivate(self, interaction: discord.Interaction, button: discord.ui.Button):
        if not isinstance(interaction.user, discord.Member) or \
                not interaction.user.guild_permissions.administrator:
            await interaction.response.send_message(
                embed=error_embed("Necesitas permisos de **Administrador** para esto."),
                ephemeral=True,
            )
            return
        await interaction.response.edit_message(embed=_deactivate_role(), view=None)


# ── Instancia del bot ─────────────────────────────────────────────────────────

intents = discord.Intents.default()
intents.message_content = True


def _get_prefix(bot_instance, message) -> str:
    return COMMAND_PREFIX


bot = commands.Bot(command_prefix=_get_prefix, intents=intents, tree_cls=CyberTree)


@bot.check
async def _global_role_check(ctx: commands.Context) -> bool:
    """Verifica el rol requerido para todos los comandos de prefijo."""
    if REQUIRED_ROLE_ID == 0:
        return True
    if ctx.guild is None:
        return False
    if ctx.author.guild_permissions.administrator:
        return True
    role = ctx.guild.get_role(REQUIRED_ROLE_ID)
    return role is not None and role in ctx.author.roles


@bot.event
async def on_command_error(ctx: commands.Context, error: commands.CommandError) -> None:
    if isinstance(error, commands.CheckFailure):
        await ctx.send(embed=role_denied_embed(), delete_after=12)
    elif isinstance(error, commands.MissingPermissions):
        await ctx.send(
            embed=error_embed("Necesitas permisos de **Administrador** para este comando."),
            delete_after=12,
        )
    elif isinstance(error, commands.RoleNotFound):
        # w!rol off → el converter falla porque "off" no es un rol válido
        if ctx.command and ctx.command.name == "rol" and \
                error.argument.lower() in ("off", "none", "0", "libre", "todos", "sin"):
            await ctx.send(embed=_deactivate_role())
        else:
            await ctx.send(
                embed=error_embed(
                    f"No se encontró el rol `{error.argument}`.\n"
                    "▸ Usa @mención, ID numérico o nombre exacto."
                )
            )


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


# ── /rol ─────────────────────────────────────────────────────────────────────

@bot.hybrid_command(name="rol", description="🔐 Establece el rol necesario para usar el bot (solo admins)")
@app_commands.describe(rol="Rol que tendrá acceso al bot. Deja vacío para ver el estado actual")
@app_commands.default_permissions(administrator=True)
@commands.has_permissions(administrator=True)
@commands.guild_only()
async def cmd_rol(ctx: commands.Context, rol: discord.Role | None = None) -> None:
    """
    Slash:  /rol           → muestra estado (botón Desactivar si hay uno activo)
            /rol @rol      → activa restricción al rol elegido
    Prefijo: w!rol         → muestra estado
             w!rol @rol    → activa restricción
             w!rol off     → desactiva restricción (manejado por on_command_error)
    """
    global REQUIRED_ROLE_ID

    if rol is None:
        # Mostrar estado actual
        if REQUIRED_ROLE_ID == 0:
            desc = "> El bot está **abierto a todos** los miembros."
            view = None
        else:
            desc = (
                f"> 🎭 **Rol activo:** <@&{REQUIRED_ROLE_ID}>\n"
                "> Solo ese rol puede interactuar con el bot."
            )
            view = RolView()
        embed = discord.Embed(
            title="🔐  ROL REQUERIDO  ·  Estado actual",
            description=(
                f"```ansi\n\u001b[1;35m◈ CONFIGURACIÓN DE ACCESO\u001b[0m\n```"
                f"{desc}"
            ),
            color=CYBER_PURPLE,
        )
        embed.set_footer(text=_cyber_footer(f"{COMMAND_PREFIX}rol @rol  ·  {COMMAND_PREFIX}rol off"))
        await ctx.send(embed=embed, view=view)
        return

    # Activar restricción al rol elegido
    REQUIRED_ROLE_ID = rol.id
    _save_env("REQUIRED_ROLE_ID", str(rol.id))
    embed = discord.Embed(
        title="🔐  ROL REQUERIDO  ·  Actualizado",
        description=(
            f"```ansi\n\u001b[1;35m◈ ACCESO RESTRINGIDO\u001b[0m\n```"
            f"> 🎭 **Rol activo:** {rol.mention}\n"
            f"> Solo los miembros con ese rol pueden usar el bot.\n"
            f"> Los **Administradores** siempre tienen acceso completo."
        ),
        color=NEON_PURPLE,
    )
    embed.set_footer(text=_cyber_footer(f"ID: {rol.id}"))
    await ctx.send(embed=embed)


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
                f"```ansi\n\u001b[1;34m⏺  GRABANDO...\u001b[0m\n```"
                f"> ⏱️ **Duración:** `{segundos}s`  `[{bar_str}]`"
            ),
            color=CYBER_BLUE,
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


# ── /sd ──────────────────────────────────────────────────────────────────────

@bot.hybrid_command(name="sd", description="💾 Explora y descarga archivos guardados en la tarjeta SD")
async def cmd_sd(ctx: commands.Context) -> None:
    await ctx.defer()
    files = list_sd_files()
    if files is None:
        await ctx.send(embed=connection_error_embed())
        return
    if not files:
        empty = discord.Embed(
            title="💾  SD CARD  ·  Vacía",
            description=(
                "```ansi\n\u001b[1;33m◈ SIN ARCHIVOS\u001b[0m\n```"
                "> No hay archivos guardados en la tarjeta SD."
            ),
            color=CYBER_PURPLE,
        )
        empty.set_footer(text=_cyber_footer())
        await ctx.send(embed=empty)
        return
    total_pages = max(1, (len(files) + _SD_FILES_PER_PAGE - 1) // _SD_FILES_PER_PAGE)
    await ctx.send(embed=_sd_embed(files, 0, total_pages), view=SDView(files))


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
        ("💾  `/sd`",               "Explora y descarga archivos de la tarjeta SD"),
        ("📊  `/estado`",            "Estado del sistema: RAM, WiFi, uptime"),
        ("🔐  `/rol [@rol]`",        "*(Admin)* Establece el rol que puede usar el bot"),
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
