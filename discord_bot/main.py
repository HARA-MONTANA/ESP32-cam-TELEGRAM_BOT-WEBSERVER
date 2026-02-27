"""
ESP32-CAM Discord Suite — Menú principal
=========================================
Ejecuta este archivo para acceder a todas las funciones:

  python main.py

Opciones del menú:
  1. Configurar credenciales  → guarda DISCORD_TOKEN, IP y puerto en .env
  2. Iniciar bot de Discord   → arranca el bot (Ctrl+C para detenerlo)
  3. Grabar video del stream  → graba X segundos y guarda como .mp4
  4. Ver configuración actual → muestra los valores del .env
  5. Salir
"""

import logging
import os
import sys
import time
from pathlib import Path

# El .env se busca en la misma carpeta que este script
ENV_FILE = Path(__file__).parent / ".env"

# ---------------------------------------------------------------------------
# Utilidades de configuración (.env)
# ---------------------------------------------------------------------------


def _load_env() -> dict[str, str]:
    """Lee el .env y devuelve un dict con los valores actuales."""
    from dotenv import dotenv_values
    defaults = {
        "DISCORD_TOKEN": "",
        "ESP32_IP": "192.168.1.100",
        "ESP32_PORT": "80",
    }
    if ENV_FILE.exists():
        saved = dotenv_values(str(ENV_FILE))
        defaults.update({k: v for k, v in saved.items() if v is not None})
    return defaults


def _save_env(key: str, value: str) -> None:
    """Escribe (o actualiza) una clave en el archivo .env."""
    from dotenv import set_key
    ENV_FILE.touch(exist_ok=True)
    set_key(str(ENV_FILE), key, value)


# ---------------------------------------------------------------------------
# Pantalla del menú
# ---------------------------------------------------------------------------

HEADER = r"""
  ╔══════════════════════════════════════════════════╗
  ║          ESP32-CAM  ·  Discord Suite             ║
  ╚══════════════════════════════════════════════════╝"""


def _clear() -> None:
    os.system("cls" if os.name == "nt" else "clear")


def _print_menu(cfg: dict[str, str]) -> None:
    _clear()
    print(HEADER)

    token_ok = bool(cfg.get("DISCORD_TOKEN"))
    token_label = "Configurado ✓" if token_ok else "NO configurado ✗"
    ip = cfg.get("ESP32_IP", "?")
    port = cfg.get("ESP32_PORT", "80")

    print(f"""
  ESP32-CAM :  http://{ip}:{port}
  Bot Token :  {token_label}

  ┌─────────────────────────────────────┐
  │  1. Configurar credenciales         │
  │  2. Iniciar bot de Discord          │
  │  3. Grabar video desde el stream    │
  │  4. Ver configuración actual        │
  │  5. Salir                           │
  └─────────────────────────────────────┘""")


def _prompt(label: str, default: str = "") -> str:
    """Lee una línea del usuario; si está vacía devuelve el valor por defecto."""
    hint = f" [{default}]" if default else ""
    value = input(f"  {label}{hint}: ").strip()
    return value if value else default


# ---------------------------------------------------------------------------
# Opción 1: Configurar credenciales
# ---------------------------------------------------------------------------


def menu_configurar(cfg: dict[str, str]) -> None:
    _clear()
    print(HEADER)
    print("\n  ── Configurar credenciales ──\n")
    print("  Deja el campo en blanco para mantener el valor actual.\n")

    # Token de Discord
    current_token = cfg.get("DISCORD_TOKEN", "")
    hidden = (current_token[:8] + "..." + current_token[-4:]) if len(current_token) > 12 else current_token
    token_hint = hidden if current_token else "(sin configurar)"
    new_token = input(f"  Discord Token [{token_hint}]: ").strip()
    if new_token:
        _save_env("DISCORD_TOKEN", new_token)
        print("  ✓ Token guardado")

    # IP de la ESP32-CAM
    new_ip = _prompt("IP de la ESP32-CAM", cfg.get("ESP32_IP", "192.168.1.100"))
    if new_ip != cfg.get("ESP32_IP"):
        _save_env("ESP32_IP", new_ip)

    # Puerto
    new_port = _prompt("Puerto del servidor web", cfg.get("ESP32_PORT", "80"))
    if not new_port.isdigit():
        print(f"  ! Puerto inválido, se mantiene {cfg['ESP32_PORT']}")
        new_port = cfg["ESP32_PORT"]
    if new_port != cfg.get("ESP32_PORT"):
        _save_env("ESP32_PORT", new_port)

    print(f"\n  ✓ Configuración guardada en {ENV_FILE}\n")
    input("  Pulsa Enter para volver al menú...")


# ---------------------------------------------------------------------------
# Opción 2: Iniciar bot de Discord
# ---------------------------------------------------------------------------


def menu_iniciar_bot(cfg: dict[str, str]) -> None:
    _clear()
    print(HEADER)
    print("\n  ── Iniciar bot de Discord ──\n")

    if not cfg.get("DISCORD_TOKEN"):
        print("  ✗ No hay token configurado. Ve a la opción 1 primero.\n")
        input("  Pulsa Enter para volver...")
        return

    ip = cfg.get("ESP32_IP", "?")
    port = cfg.get("ESP32_PORT", "80")
    print(f"  ESP32-CAM : http://{ip}:{port}")
    print("  Presiona Ctrl+C para detener el bot.\n")
    print("  " + "─" * 46)

    # Configurar logging básico para ver la actividad del bot en la terminal
    logging.basicConfig(
        level=logging.INFO,
        format="  %(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )

    try:
        import bot as discord_bot
        discord_bot.run()
    except KeyboardInterrupt:
        print("\n  Bot detenido.\n")
    except SystemExit as exc:
        print(f"\n  Error: {exc}\n")

    input("  Pulsa Enter para volver al menú...")


# ---------------------------------------------------------------------------
# Opción 3: Grabar video desde el stream
# ---------------------------------------------------------------------------


def menu_grabar(cfg: dict[str, str]) -> None:
    _clear()
    print(HEADER)
    print("\n  ── Grabar video desde el stream ──\n")

    ip = cfg.get("ESP32_IP", "192.168.1.100")
    port = cfg.get("ESP32_PORT", "80")
    stream_url = f"http://{ip}:{port}/stream"

    print(f"  Stream   : {stream_url}")

    # Duración
    dur_str = _prompt("Duración en segundos", "10")
    if not dur_str.isdigit() or int(dur_str) <= 0:
        print("  ! Duración inválida, usando 10 segundos")
        dur_str = "10"
    duration = int(dur_str)

    # Carpeta de salida
    recordings_dir = Path(__file__).parent / "recordings"
    recordings_dir.mkdir(exist_ok=True)

    from datetime import datetime
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    default_name = f"grabacion_{ts}.mp4"
    out_name = _prompt("Nombre del archivo", default_name)
    if not out_name.endswith(".mp4"):
        out_name += ".mp4"

    output_path = str(recordings_dir / out_name)

    print(f"\n  Grabando {duration}s → {output_path}\n")

    # Barra de progreso simple en terminal
    last_print = [0.0]

    def on_progress(elapsed: float, total: float, frames: int) -> None:
        if elapsed - last_print[0] >= 1.0:
            pct = min(100, int(elapsed / total * 100))
            bar = "█" * (pct // 5) + "░" * (20 - pct // 5)
            print(f"\r  [{bar}] {pct:3d}%  {elapsed:.0f}/{total}s  {frames} frames", end="", flush=True)
            last_print[0] = elapsed

    from recorder import record_stream
    success = record_stream(stream_url, duration, output_path, on_progress=on_progress)

    print()  # Nueva línea tras la barra

    if success:
        size_kb = os.path.getsize(output_path) / 1024
        print(f"\n  ✓ Video guardado: {output_path}")
        print(f"    Tamaño: {size_kb:.1f} KB\n")
    else:
        print("\n  ✗ Error al grabar el video.")
        print("    Verifica que la ESP32-CAM esté encendida y el stream accesible.\n")

    input("  Pulsa Enter para volver al menú...")


# ---------------------------------------------------------------------------
# Opción 4: Ver configuración
# ---------------------------------------------------------------------------


def menu_ver_config(cfg: dict[str, str]) -> None:
    _clear()
    print(HEADER)
    print("\n  ── Configuración actual ──\n")

    token = cfg.get("DISCORD_TOKEN", "")
    if token:
        visible = token[:8] + "..." + token[-4:] if len(token) > 12 else token
    else:
        visible = "(no configurado)"

    print(f"  Archivo .env : {ENV_FILE}")
    print(f"  DISCORD_TOKEN: {visible}")
    print(f"  ESP32_IP     : {cfg.get('ESP32_IP', '?')}")
    print(f"  ESP32_PORT   : {cfg.get('ESP32_PORT', '?')}")
    print(f"  Stream URL   : http://{cfg.get('ESP32_IP', '?')}:{cfg.get('ESP32_PORT', '80')}/stream")
    print()
    input("  Pulsa Enter para volver al menú...")


# ---------------------------------------------------------------------------
# Bucle principal del menú
# ---------------------------------------------------------------------------


def main() -> None:
    while True:
        cfg = _load_env()
        _print_menu(cfg)

        choice = input("\n  Opción: ").strip()

        if choice == "1":
            menu_configurar(cfg)
        elif choice == "2":
            menu_iniciar_bot(cfg)
        elif choice == "3":
            menu_grabar(cfg)
        elif choice == "4":
            menu_ver_config(cfg)
        elif choice == "5":
            _clear()
            print("\n  Hasta luego!\n")
            sys.exit(0)
        else:
            # Opción inválida: simplemente redibujar el menú
            pass


if __name__ == "__main__":
    main()
