<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Install Passport Claude Radio

[Shareable installation page](https://zs20200101.github.io/passport-claude-radio/install/) · [Source and updates](https://github.com/zs20200101/passport-claude-radio)

This guide uses commands relative to the **extracted release ZIP root**. Use Windows PowerShell and Python 3.10+. ESP-IDF is not needed for prebuilt installation. Extract into a permanent folder: the optional hook refers to its Python and script paths, so moving/deleting it requires reinstalling the hook.

## Flash and Wi-Fi

1. Connect a FoloToy AI Passport (ESP32-C3 / 8 MB) with a USB data cable. Close serial monitors.
2. Open the [official browser flasher](https://ai-passport.folotoy.cn/tools/web-flasher/) in desktop Chrome/Edge. Choose `firmware/FoloToy-AI-Passport-full.bin` and flash at **0x0**. The full image can reset Wi-Fi and pairing settings. No full-chip erase is needed.
3. Connect your phone to the device's `LEO Radio` hotspot, open `http://192.168.4.1`, and enter your own **2.4 GHz** Wi-Fi credentials. The computer and device must be on the same reachable private LAN; guest/client isolation can prevent pairing.

## Companion, pairing and optional Claude Code hook

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/claude_lan_bridge/requirements.txt
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py
.\.venv\Scripts\python.exe tools/claude_lan_bridge/bridge.py --config "$env:LOCALAPPDATA\PassportClaude\config.json"
```

Keep this window running. For Phoenix Info install FFmpeg with `libmp3lame` separately and ensure the bridge process finds `ffmpeg` on PATH. The package does not bundle FFmpeg. Other MP3 radios play directly. Network location/weather and lunar updates also require the companion.

In a second PowerShell at the same folder, replace example COM5 and 192.168.1.10 with your actual Passport serial port and **computer LAN IPv4**:

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --serial COM5 --host 192.168.1.10
```

After the device shows the connected status, USB may be unplugged for Wi-Fi use. Reserve the computer IP on the router; pair again after address changes. If blocked, allow TCP 8765 only from your trusted private LAN in the computer firewall. The setup does not change firewall settings.

Only after the bridge and pairing work, optionally enable Claude Code permission control:

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --install-hook
```

Restart your Claude Code session. Only Code sessions that actually dispatch `PermissionRequest` are supported; ordinary chat and Windows dialogs are outside this integration. A settings backup is made before hook changes. With the hook enabled, an offline device/bridge can cause a request to wait and then be denied. To restore the normal flow, remove only the Passport entry from `hooks.PermissionRequest` in `~/.claude/settings.json`, preserving other hooks, then restart the session. The bridge can be stopped with Ctrl+C.

## Controls and upgrades

Home: single UP/DOWN adjusts volume by 5, double UP/DOWN changes station, OK play/pause, hold OK opens settings. Permission overlay: single UP approves, DOWN denies; playback pauses and resumes afterward if still requested.

For existing installations of this app with the **same partition table**, experienced users can flash only `firmware/FoloToy-AI-Passport.bin` at **0x10000**, preserving compatible NVS. Never flash the app-only file at 0x0. This upgrade path does not apply blindly to unrelated firmware.

## Verification and privacy

Compare file SHA256 values with `SHA256SUMS.txt` using `Get-FileHash`. The package's `release-manifest.json` records offsets and image identities. The exact hardware-accepted merged firmware hash is `71ac40b4a0f10beed7560d90a8c12287d22e14fed459f5d726ce5ec28a48a1ee`. Its embedded version is `a06235e-dirty`; the release label is v1.0.0-claude-radio.

Each user creates a private configuration and a new pairing key. No credentials, NVS, logs or debug files are shared. HMAC authenticates approval messages; LAN HTTP is not encrypted. Use trusted networks, never expose the bridge port publicly. Network IP location can be inaccurate or reflect VPN egress. Radio sources and services may become unavailable. Physical controls, display, Phoenix audio and synthetic interruption/resume passed; actual Desktop hook dispatch, every station and endurance remain unverified.

MIT application licenses and font OFL notices are included. Broadcast content remains its providers' property. For the full bilingual design/build guide, see [the project](https://github.com/zs20200101/passport-claude-radio).

The source manifest records separate SHA256 values for the exact Windows build inputs and committed Git bytes; CRLF/LF differences are checked after line-ending normalization.
