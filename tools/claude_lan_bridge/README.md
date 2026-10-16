<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Claude LAN radio companion

The Passport shows a clock, date, Chinese lunar calendar, cached weather and city radio. A Claude Code `PermissionRequest` pauses audio and displays a separate overlay: **UP approves, DOWN denies**. The prompt clears after a decision, expiration or connection loss. Playback resumes only if the user/timer still wants it. The sleep timer pauses audio while keeping Wi-Fi and approvals available.

## Prepare this computer

Use Python 3.10 or later and ESP-IDF **5.5.3**. From the repository root in PowerShell:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/claude_lan_bridge/requirements.txt
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py
.\.venv\Scripts\python.exe tools/claude_lan_bridge/bridge.py --config "$env:LOCALAPPDATA\PassportClaude\config.json"
```

Start the bridge and finish device pairing first. Once the device is online, run `setup.py --install-hook` to enable the global permission hook; enabling it earlier makes requests wait and deny while no device is paired.

Setup creates a private 64-hex pairing token in the config above, and limits its directory ACL to the current user and SYSTEM. With `--install-hook`, it merges only this integration's `PermissionRequest` hook into `~/.claude/settings.json`. Existing settings are backed up beside that file before changes. Repeating setup preserves the token and does not duplicate hooks. The hook uses current Claude Code's executable/argument form to handle spaces and Unicode in paths. Restart the Code session after installing it. If the host bridge is stopped, this hook denies requests; remove only the Passport hook entry to return to the previous permission flow.

The integration handles Claude **Code sessions**, including a Desktop Code session that loads these local hooks. Ordinary Claude chat and arbitrary Windows permission dialogs are outside this integration. A Desktop session's actual hook loading and sandbox/network permission behavior require verification with the installed Claude version. Existing auto-allow rules may prevent a permission prompt from appearing. No CLI login, account settings or permission allowlists are changed by setup.

## Pair the device after authorized flashing

1. On first boot, connect a phone to the `LEO Radio` hotspot and open `http://192.168.4.1` if the portal does not appear. Choose a 2.4 GHz Wi-Fi network on the same LAN as this PC. Existing saved Wi-Fi is retained when using a compatible segmented flash.
2. Identify the intended Passport USB serial port and the PC's LAN IPv4 address. Pair with the physical USB cable:

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --serial COM5 --host 192.168.1.10
```

The port and address are examples. This command writes one atomic NVS pairing blob; it never flashes. Wait for the confirmation before unplugging USB. Keep the PC address stable with a router DHCP reservation, or pair again after its address changes. Wi-Fi client isolation and Windows Firewall can block inbound TCP 8765; if needed, permit the bridge only on the intended private LAN. No firewall rule is installed automatically.

The device displays `Claude connected` in its localized UI after signed polling succeeds. A prompt wakes and holds the screen; unrelated button actions are consumed until it clears. A button press begun before the prompt cannot approve it. Long commands scroll, but summaries are bounded: inspect the full command on the PC when needed. USB is only required for initial flashing/pairing; the bridge runs on the PC during wireless operation.

The **Radio categories** selector keeps existing cities (including Kunming), CNR and Guangdong, and adds **Phoenix Info**. Hong Kong and online listening have been removed. Existing city indices 0-10 remain stable; retired Hong Kong/listening selections migrate to Kunming using a catalog-version marker. On the home screen, single UP/DOWN changes volume by +5/−5 (10–100); double UP/DOWN selects the previous/next station. The current volume is shown beside the channel number. OK pauses/resumes; long OK opens settings. Settings retain single-click navigation. Permission overlays retain single UP to approve and single DOWN to deny; radio gestures cannot act on an overlay.

Phoenix Info uses the public Phoenix audio-only HLS/AAC source on the broadcaster's domain. The paired PC converts only this fixed source to 64 kbit/s mono MP3 through FFmpeg and serves `/radio/phoenix.mp3` on the existing bridge port. Install FFmpeg with `libmp3lame` and make `ffmpeg` discoverable on the bridge process PATH. Keep the PC and bridge running for Phoenix; the other MP3 radio sources remain direct. The public-audio route accepts only private/loopback clients, permits two listeners, runs no shell, accepts no custom URL, and tears down its subprocess on disconnect or stalled output. It exposes no pairing credentials or permission controls. Existing signed permission routes remain unchanged.

The device downloads and decodes in separate tasks, using a 32 KiB compressed-audio ring and 24 KiB prebuffer before first playback or after starvation. At 64 kbit/s, this holds approximately four seconds of audio (three seconds prebuffer). Each streaming read waits at most 700 ms so a permission prompt can stop audio within its 1.5-second deadline. Transient reads retry for up to eight seconds; exhausted connections drain buffered audio before reconnecting. Audio-level animation updates at most ten times per second. These measures absorb short network delays but cannot guarantee continuity during a prolonged Wi-Fi/upstream outage. The unchanged performance Wi-Fi setting disables modem sleep while connected.

Directory station names retain Chinese text, and duplicate stream URLs are removed. Physical USB accepts `PASSPORT_CITY <choice>` (the exact UTF-8 city/category name) followed by a newline; `PASSPORT_CITY_QUEUED` means the separate action was queued for persistence/discovery, and `PASSPORT_CITY_ERROR` rejects unknown names or a full queue. This command cannot inject keys or approve Claude prompts. Weather city remains independently automatic.

## Weather and calendar

Weather defaults to automatic city detection from this computer's current network exit. For Chinese networks it first requests the domestic city from myip.ipip.net over a direct connection (application HTTP proxies bypassed), then resolves that city and province to coordinates through Open-Meteo geocoding. If domestic lookup is unavailable on first use or reports another country, ipwho.is is the fallback. Once a domestic city is known, a domestic-service outage retains it with a location-cache marker instead of silently switching to a conflicting global-IP result. City and weather refresh every 15 minutes. Geocoding failure also retains the previous location and retries. These requests contain no Claude prompt, transcript or pairing token, and the observed public IP is not forwarded to geocoding. Provider databases and VPN exits can still differ from physical location; this is network location, not GPS.

To switch an existing configuration back to automatic detection:

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --auto-location
```

For an optional manual override, supply your chosen city and coordinates together:

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --city CITY --latitude LAT --longitude LON
```

Restart the bridge after changes. The host obtains current weather over HTTPS from Open-Meteo, refreshes every 15 minutes and retains the last value with a cache marker on failure. Coordinates are sent to that service. Radio city selection and weather city are independently configured. Lunar dates are calculated offline by `lunar-python`; the clock uses Asia/Shanghai (UTC+8), host synchronization and device SNTP. Without a fresh host update the device marks lunar/weather data stale. Radio lookup/streams depend on the upstream public services and station availability; MP3 decoding is enabled.

## Protocol and security behavior

Protocol v2 signs requests with HMAC-SHA256 over `METHOD + LF + PATH + LF + nonce + LF + body`; nonce is 16 random bytes encoded as 32 lowercase hex characters. Headers are `X-Passport-Nonce` and `X-Claude-Bridge-Signature`. Responses use `X-Passport-Response` over `nonce + LF + decimal_status + LF + body`. Token bytes are the UTF-8 bytes of the 64-hex token, not decoded binary.

The loopback-only `/hook` rejects browser Origins and requires authentication. The hook forwards only bounded tool name (32 UTF-8 bytes), summary (256), and working directory (160), never a transcript. Command/path takes priority over description. Device endpoints are `/v1/pending`, `/v1/decision`, `/v1/idle`. A decision requires a delivered request ID and its random one-time challenge; stale/repeated/undelivered approvals are rejected. Requests expire after at most 120 seconds; after delivery, a five-second polling lease detects disconnect. The queue is bounded to eight. Failure, timeout and overflow deny. A lost decision acknowledgement can leave the device uncertain even when the host already accepted that decision; check the Code session.

HTTP payloads are authenticated **without encryption**: LAN observers can read the bounded prompt metadata. Keep the bridge on a trusted LAN, do not forward its port to the Internet, and do not publish the private config. The unsigned `/health` exposes protocol version only. Re-pairing updates credentials atomically and denies any active old request.

## Validate and build

```bash
PYTHON=.venv/Scripts/python.exe ./tools/validate.sh --static
./tools/validate.sh
```

On Windows, activate the installed ESP-IDF PowerShell environment first and run the gate using Git Bash from that environment. The native Windows firmware stage makes an isolated ASCII copy under `C:/ai-passport-validation` to avoid ESP-IDF Kconfig failures with Unicode project paths. It uses tracked defaults, preserves the source/local sdkconfig, verifies the merged image, archives matching ELF/MAP/images and records source hashes. The temporary copy is removed after the stage. The merged image is `build/FoloToy-AI-Passport-full.bin`; it is for offset **0x0**, may reset stored data, and must never be confused with the application-only binary.

Host tests exercise real HTTP/hook subprocesses, authentication/tampering/replay, queue/timeout/disconnect, one-shot decisions, settings merge/backup, calendar fixtures, weather-cache failure, firmware-independent state transitions and generated glyph coverage. They do not verify button electronics, visible glyph rendering, audio, RAM headroom, Wi-Fi provisioning or Desktop's actual event dispatch. Those require separately authorized device testing.

## Sources and licenses

- Radio app code and title font: [leo-radio](https://github.com/leo0183/leo-radio), MIT; notice retained in [leo-radio.txt](../../LICENSES/leo-radio.txt). Upstream optional video/easter-egg resources are not included in this build.
- Wi-Fi component: `78/esp-wifi-connect` 3.2.2, manifest-declared MIT, pinned source commit in its manifest.
- Noto Sans SC and generated body font: SIL OFL 1.1, [font license](../../assets/fonts/OFL.txt), provenance/reproduction in [assets](../../assets/README.md).
- `lunar-python` 1.4.8: MIT; `pyserial` 3.5: BSD. Versions are pinned in `requirements.txt`.
- Design follows the requested Passport agent tutorial and Anthropic's current Claude Code hook reference. Consult the official reference when upgrading Claude, especially `PermissionRequest` decision JSON and hook executable/argument support.
