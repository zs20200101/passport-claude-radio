"""Fixed public Phoenix audio relay; no arbitrary input URL or executable API."""
from __future__ import annotations
import ipaddress
import queue
import shutil
import subprocess
import threading

PHOENIX_AUDIO = "http://playtv-live.ifeng.com/live/06OLEEWQKN4_audio.m3u8"
RADIO_PATH = "/radio/phoenix.mp3"


def command(executable):
    return [executable, "-nostdin", "-hide_banner", "-loglevel", "error",
            "-rw_timeout", "8000000", "-reconnect", "1", "-reconnect_streamed", "1",
            "-reconnect_delay_max", "2", "-fflags", "+discardcorrupt",
            "-i", PHOENIX_AUDIO, "-vn", "-ac", "1", "-ar", "24000",
            "-c:a", "libmp3lame", "-b:a", "64k", "-flush_packets", "1",
            "-f", "mp3", "pipe:1"]


class RadioRelay:
    def __init__(self):
        self.slots = threading.BoundedSemaphore(2)

    def serve(self, handler):
        # This public-audio route has no access to hook/config/decision objects.
        address = ipaddress.ip_address(handler.client_address[0])
        if handler.path != RADIO_PATH or not (address.is_private or address.is_loopback):
            handler.reply(404)
            return
        executable = shutil.which("ffmpeg")
        if not executable:
            handler.reply(503, {"error": "radio transcoder unavailable"})
            return
        if not self.slots.acquire(blocking=False):
            handler.reply(503, {"error": "radio listeners full"})
            return
        process = None
        reader = None
        stop = threading.Event()
        blocks = queue.Queue(maxsize=8)
        try:
            process = subprocess.Popen(command(executable), stdin=subprocess.DEVNULL,
                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

            def read_audio():
                try:
                    while not stop.is_set():
                        block = process.stdout.read(1024)
                        while not stop.is_set():
                            try:
                                blocks.put(block, timeout=0.2)
                                break
                            except queue.Full:
                                continue
                        if not block:
                            return
                except (OSError, ValueError):
                    pass

            reader = threading.Thread(target=read_audio, daemon=True)
            reader.start()
            first = blocks.get(timeout=12)
            if not first:
                handler.reply(502, {"error": "Phoenix upstream unavailable"})
                return
            handler.connection.settimeout(3)
            handler.send_response(200)
            handler.send_header("Content-Type", "audio/mpeg")
            handler.send_header("Cache-Control", "no-store")
            handler.send_header("Connection", "close")
            handler.end_headers()
            handler.close_connection = True
            handler.wfile.write(first)
            while not stop.is_set():
                block = blocks.get(timeout=12)
                if not block:
                    break
                handler.wfile.write(block)
        except queue.Empty:
            # End a stalled stream, allowing the device to reconnect boundedly.
            handler.close_connection = True
        except (OSError, ValueError, subprocess.SubprocessError):
            handler.close_connection = True
        finally:
            stop.set()
            if process is not None:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=5)
                if reader:
                    reader.join(timeout=2)
                process.stdout.close()
            self.slots.release()
