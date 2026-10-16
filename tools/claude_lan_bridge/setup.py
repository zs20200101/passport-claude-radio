#!/usr/bin/env python3
"""Create private host config, optionally merge hooks and pair over physical USB."""
from __future__ import annotations
import argparse
import datetime as dt
import ipaddress
import json
import os
import re
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import time

DEFAULT_CONFIG = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "PassportClaude" / "config.json"


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temp = tempfile.mkstemp(prefix=".passport-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as file:
            json.dump(value, file, ensure_ascii=False, indent=2)
            file.write("\n")
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def merge_hook(settings, handler):
    value = json.loads(json.dumps(settings))
    entries = value.setdefault("hooks", {}).setdefault("PermissionRequest", [])
    kept = []
    for entry in entries:
        hooks = entry.get("hooks", [])
        remaining = [h for h in hooks if not ("claude_lan_bridge" in str(h.get("command", "")) + str(h.get("args", []))
                     and "permission_hook.py" in str(h.get("command", "")) + str(h.get("args", [])))]
        if remaining or not hooks:
            kept.append({**entry, "hooks": remaining})
    kept.append({"matcher": "", "hooks": [handler]})
    value["hooks"]["PermissionRequest"] = kept
    return value


def install_hook(settings_path, config_path):
    original = json.loads(settings_path.read_text(encoding="utf-8-sig")) if settings_path.exists() else {}
    # Current Claude Code exec form preserves Unicode/space paths without a shell.
    handler = {"type": "command", "command": str(Path(sys.executable).resolve()),
               "args": [str(Path(__file__).with_name("permission_hook.py").resolve()),
                        "--config", str(config_path.resolve())], "timeout": 140}
    updated = merge_hook(original, handler)
    if updated == original:
        return
    if settings_path.exists():
        stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
        shutil.copy2(settings_path, settings_path.with_name(settings_path.name+".passport-backup-"+stamp))
    atomic_json(settings_path, updated)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--host", help="LAN IPv4 address of this computer (required when pairing)")
    parser.add_argument("--port", type=int, default=None)
    parser.add_argument("--serial", help="USB port, e.g. COM5; writes pairing data, never flashes")
    parser.add_argument("--auto-location", action="store_true", help="locate weather city from the public network IP")
    parser.add_argument("--city", help="weather city; requires latitude and longitude")
    parser.add_argument("--latitude", type=float)
    parser.add_argument("--longitude", type=float)
    parser.add_argument("--install-hook", action="store_true")
    parser.add_argument("--settings", type=Path, default=Path.home()/".claude"/"settings.json")
    args = parser.parse_args()
    if args.host:
        ipaddress.IPv4Address(args.host)
    if args.serial and not args.host:
        parser.error("--serial requires --host: choose the computer's LAN IPv4, not the device IP")
    if args.port is not None and not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    if args.auto_location and any(value is not None for value in (args.city, args.latitude, args.longitude)):
        parser.error("--auto-location cannot be combined with a manual city or coordinates")
    if not args.city and not args.auto_location and (args.latitude is not None or args.longitude is not None):
        parser.error("manual coordinates require --city, --latitude and --longitude together")
    if args.city and (args.latitude is None or args.longitude is None):
        parser.error("--city requires --latitude and --longitude")
    if args.latitude is not None and not -90 <= args.latitude <= 90:
        parser.error("invalid latitude")
    if args.longitude is not None and not -180 <= args.longitude <= 180:
        parser.error("invalid longitude")
    config = json.loads(args.config.read_text(encoding="utf-8")) if args.config.exists() else {}
    config.setdefault("token", secrets.token_hex(32))
    config.setdefault("port", 8765)
    config.setdefault("bind", "0.0.0.0")
    config.setdefault("timeout", 120)
    for key in ("port", "city", "latitude", "longitude"):
        if getattr(args, key) is not None:
            config[key] = getattr(args, key)
    config.setdefault("location_mode", "manual" if config.get("city") else "auto")
    if args.auto_location:
        config["location_mode"] = "auto"
        for key in ("city", "latitude", "longitude"):
            config.pop(key, None)
    elif args.city:
        config["location_mode"] = "manual"
    atomic_json(args.config, config)
    if os.name == "nt":
        # Restrict only our private directory: no repository or user-wide ACL edits.
        # Match the ASCII SID directly: localized account names use console encodings
        # that need not match Python's UTF-8 mode or the Windows ANSI code page.
        identity = subprocess.check_output(["whoami", "/user", "/fo", "csv", "/nh"])
        sid = re.search(rb'"(S-\d+(?:-\d+)+)"', identity)
        if not sid:
            raise RuntimeError("Could not resolve current Windows user SID")
        account = "*" + sid.group(1).decode("ascii")
        result = subprocess.run(["icacls", str(args.config.parent), "/inheritance:r", "/grant:r",
                  f"{account}:(OI)(CI)F", "*S-1-5-18:(OI)(CI)F"], capture_output=True)
        if result.returncode:
            raise RuntimeError("Could not restrict private config ACL")
    else:
        args.config.parent.chmod(0o700)
        args.config.chmod(0o600)
    if args.install_hook:
        install_hook(args.settings, args.config)
        print("Claude PermissionRequest hook merged; restart Claude Code to load it.")
    if args.serial:
        import serial
        payload = json.dumps({"host": args.host, "port": config["port"], "token": config["token"]}, separators=(",", ":"))
        with serial.Serial(args.serial, 115200, timeout=0.5, write_timeout=3, rtscts=False, dsrdtr=False) as port:
            port.reset_input_buffer()
            port.write(("\nPASSPORT_PAIR "+payload+"\n").encode())
            deadline = time.monotonic()+12
            received = b""
            while time.monotonic()<deadline:
                received = (received+port.read(512))[-2048:]
                if b"PASSPORT_PAIR_OK" in received:
                    print("Device pairing saved in NVS. You may unplug USB and use Wi-Fi.")
                    break
                if b"PASSPORT_PAIR_ERROR" in received:
                    raise RuntimeError("Device rejected pairing configuration")
            else:
                raise RuntimeError("Pairing timed out; verify this firmware and the intended USB port")
    print("Private bridge config:", args.config)
    print("Start:", subprocess.list2cmdline([sys.executable, str(Path(__file__).with_name("bridge.py")), "--config", str(args.config)]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
