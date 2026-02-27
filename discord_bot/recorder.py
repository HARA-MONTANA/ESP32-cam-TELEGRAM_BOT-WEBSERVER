"""
Módulo de grabación: stream MJPEG → archivo .mp4
=================================================
Lee el stream MJPEG de la ESP32-CAM fotograma a fotograma,
parsando los marcadores JPEG (SOI 0xFF 0xD8 / EOI 0xFF 0xD9)
directamente del flujo HTTP, y escribe el resultado en un .mp4
usando OpenCV VideoWriter.

No almacena todos los frames en memoria: escribe frame a frame
para mantener bajo el uso de RAM incluso en grabaciones largas.

Uso:
    from recorder import record_stream

    ok = record_stream(
        stream_url="http://192.168.1.100/stream",
        duration=15,
        output_path="grabacion.mp4",
        on_progress=None,   # opcional: función(elapsed, total, frames)
    )
"""

import logging
import os
import time

import cv2
import numpy as np
import requests

log = logging.getLogger("esp32-recorder")

# FPS que se asigna al VideoWriter.
# La ESP32-CAM suele emitir entre 8 y 20 fps; 15 es un buen valor por defecto.
# El video se reproducirá a esta velocidad independientemente del FPS real.
OUTPUT_FPS: float = 15.0

# Tamaño máximo del buffer de bytes sin parsear (2 MB).
# Si crece más, se descarta la mitad para evitar consumo excesivo de RAM.
MAX_BUFFER_BYTES: int = 2 * 1024 * 1024


def record_stream(
    stream_url: str,
    duration: int,
    output_path: str,
    on_progress=None,
) -> bool:
    """
    Graba el stream MJPEG de la ESP32-CAM durante `duration` segundos.

    Args:
        stream_url:    URL del stream (ej. http://192.168.1.100/stream)
        duration:      Segundos a grabar
        output_path:   Ruta del archivo .mp4 de salida
        on_progress:   Callable opcional(elapsed_s, total_s, frames_written)
                       para mostrar progreso en CLI. None para modo silencioso.

    Returns:
        True si la grabación fue exitosa y el archivo existe con contenido.
    """
    log.info("Conectando al stream: %s", stream_url)

    try:
        response = requests.get(
            stream_url,
            stream=True,
            timeout=10,
            headers={"Connection": "keep-alive"},
        )
        response.raise_for_status()
    except requests.RequestException as exc:
        log.error("No se pudo conectar al stream: %s", exc)
        return False

    writer: cv2.VideoWriter | None = None
    buffer = b""
    frames_written = 0
    recording_started: float | None = None
    start_connect = time.time()

    log.info("Grabando %d segundos → %s", duration, output_path)

    try:
        for chunk in response.iter_content(chunk_size=4096):
            # --- Timeout de seguridad: si no llega el primer frame en 10s ---
            if recording_started is None and time.time() - start_connect > 10:
                log.error("Timeout: no se recibió ningún frame en 10 segundos")
                return False

            # --- Condición de fin de grabación ---
            if recording_started is not None:
                elapsed = time.time() - recording_started
                if elapsed >= duration:
                    break

            buffer += chunk

            # Evitar buffer ilimitado si no se parsean frames
            if len(buffer) > MAX_BUFFER_BYTES:
                log.warning("Buffer demasiado grande, descartando datos viejos")
                buffer = buffer[-(MAX_BUFFER_BYTES // 2):]

            # --- Parsear todos los frames JPEG disponibles en el buffer ---
            while True:
                soi = buffer.find(b"\xff\xd8")   # JPEG Start Of Image
                if soi == -1:
                    break

                eoi = buffer.find(b"\xff\xd9", soi + 2)  # JPEG End Of Image
                if eoi == -1:
                    # Frame incompleto, esperar más datos
                    buffer = buffer[soi:]          # descartar basura anterior
                    break

                jpeg_data = buffer[soi : eoi + 2]
                buffer = buffer[eoi + 2:]          # avanzar el buffer

                # Decodificar JPEG con OpenCV
                nparr = np.frombuffer(jpeg_data, dtype=np.uint8)
                frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
                if frame is None:
                    log.debug("Frame JPEG inválido, ignorando")
                    continue

                # --- Inicializar VideoWriter con las dimensiones del primer frame ---
                if writer is None:
                    height, width = frame.shape[:2]
                    log.info("Resolución del stream: %dx%d", width, height)

                    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
                    writer = cv2.VideoWriter(output_path, fourcc, OUTPUT_FPS, (width, height))

                    if not writer.isOpened():
                        log.error("No se pudo crear el archivo de video: %s", output_path)
                        return False

                    recording_started = time.time()

                # Escribir frame al video
                writer.write(frame)
                frames_written += 1

                # Callback de progreso (para el menú CLI)
                if on_progress is not None and recording_started is not None:
                    on_progress(time.time() - recording_started, duration, frames_written)

    except requests.RequestException as exc:
        log.error("Error leyendo el stream: %s", exc)
    except Exception as exc:
        log.error("Error inesperado durante la grabación: %s", exc)
    finally:
        if writer is not None:
            writer.release()
        response.close()

    if frames_written == 0:
        log.error("No se capturaron frames del stream")
        return False

    actual_time = time.time() - recording_started if recording_started else 0
    real_fps = frames_written / actual_time if actual_time > 0 else 0
    log.info(
        "Grabación finalizada: %d frames en %.1f s (%.1f fps real) → %s",
        frames_written,
        actual_time,
        real_fps,
        output_path,
    )

    return os.path.exists(output_path) and os.path.getsize(output_path) > 0
