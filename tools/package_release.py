#!/usr/bin/env python3
"""Package a source-matched, verified firmware archive without private build data."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TAG = "v1.0.0-claude-radio"
ZIP_NAME = "Passport-Claude-Radio-v1.0.0.zip"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True)+"\n").encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--source-manifest", type=Path, default=ROOT/"build/claude-source-manifest.json")
    parser.add_argument("--output", type=Path, default=ROOT/"build/releases"/TAG)
    args = parser.parse_args()
    subprocess.run([sys.executable, str(ROOT/"tools/archive_firmware.py"), "verify", str(args.archive)], check=True)
    record = json.loads((args.archive/"manifest.json").read_text(encoding="utf-8"))
    source = json.loads(args.source_manifest.read_text(encoding="utf-8"))
    if source["full_image_sha256"] != record["full_bin_sha256"]:
        raise SystemExit("Source manifest belongs to a different firmware")
    for name, expected in source["source_sha256"].items():
        file = (ROOT/name).resolve()
        if not file.is_relative_to(ROOT) or not file.is_file() or digest(file.read_bytes()) != expected:
            raise SystemExit("Firmware source mismatch: "+name)
    # Explicit input whitelist: never copy local configs, logs, NVS, ELF or MAP files.
    entries = {}
    for name in ("FoloToy-AI-Passport-full.bin", "FoloToy-AI-Passport.bin"):
        entries["firmware/"+name] = (args.archive/name).read_bytes()
    for name in ("setup.py", "bridge.py", "permission_hook.py", "companion.py", "radio_relay.py",
                 "requirements.txt", "claude-settings.snippet.json"):
        entries["tools/claude_lan_bridge/"+name] = (ROOT/"tools/claude_lan_bridge"/name).read_bytes()
    for name in ("README.md", "README.zh_CN.md"):
        entries[name] = (ROOT/"tools/distribution"/name).read_bytes()
    for name in ("LICENSE", "LICENSES/leo-radio.txt", "LICENSES/esp-wifi-connect.txt",
                 "LICENSES/README.md", "LICENSES/README.zh_CN.md", "assets/fonts/OFL.txt"):
        entries[name] = (ROOT/name).read_bytes()
    for file in sorted((ROOT/"LICENSES/firmware").rglob("*")):
        if file.is_file():
            entries[file.relative_to(ROOT).as_posix()] = file.read_bytes()
    # These keys contain hashes and relative paths only, unlike private delivery records.
    public_source = {key: source[key] for key in ("project_version", "idf_version", "full_image_sha256", "source_sha256")}
    entries["source-inputs.json"] = encoded(public_source)
    release = {"schema_version": 1, "release": TAG, "target": record["target"],
               "flash_size_bytes": record["flash_size_bytes"],
               "full_image_offset": 0, "application_offset": record["image_offsets"]["FoloToy-AI-Passport.bin"],
               "full_image_may_reset_settings": True,
               "partition_table_sha256": record["files"]["partition_table/partition-table.bin"]["sha256"],
               "app_descriptor": record["app_descriptor"],
               "files": {name: {"sha256": digest(data), "size": len(data)} for name, data in sorted(entries.items())}}
    entries["release-manifest.json"] = encoded(release)
    entries["SHA256SUMS.txt"] = "".join(f"{digest(data)}  {name}\n" for name, data in sorted(entries.items())).encode()
    args.output.mkdir(parents=True, exist_ok=True)
    zip_path = args.output/ZIP_NAME
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo("Passport-Claude-Radio/"+name)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    # Verify the exact downloadable archive, not merely the files copied into it.
    with zipfile.ZipFile(zip_path) as archive:
        if archive.testzip() is not None:
            raise SystemExit("ZIP integrity failure")
        assert set(archive.namelist()) == {"Passport-Claude-Radio/"+name for name in entries}
        for name, data in entries.items():
            assert archive.read("Passport-Claude-Radio/"+name) == data, name
    assets = {ZIP_NAME: zip_path.read_bytes()}
    for name in ("FoloToy-AI-Passport-full.bin", "FoloToy-AI-Passport.bin"):
        assets[name] = entries["firmware/"+name]
    for name in ("release-manifest.json", "source-inputs.json"):
        assets[name] = entries[name]
    for name, data in assets.items():
        if name != ZIP_NAME:
            (args.output/name).write_bytes(data)
    (args.output/"SHA256SUMS.txt").write_text("".join(f"{digest(data)}  {name}\n" for name, data in sorted(assets.items())), encoding="utf-8", newline="\n")
    print(f"Package: PASS ({len(entries)} files, {zip_path.stat().st_size} ZIP bytes)")
    print(f"Firmware source match: PASS ({len(source['source_sha256'])} inputs)")
    print(f"Merged image SHA256: {record['full_bin_sha256']}")
    print(f"ZIP SHA256: {digest(assets[ZIP_NAME])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
