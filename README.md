<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Passport Claude Radio

A radio, clock and Claude Code permission remote for **FoloToy AI Passport (ESP32-C3, 8 MB Flash)**.

[Installation page](https://zs20200101.github.io/passport-claude-radio/install/) · [Download release](https://github.com/zs20200101/passport-claude-radio/releases/latest) · [Chinese guide](README.zh_CN.md)

- Bold clock, date, offline lunar calendar and weather based on the companion computer's network location.
- City radio including Kunming, CNR and Guangdong, plus Phoenix Info. Hong Kong and StoryFM are removed.
- A signed LAN bridge displays Claude Code permission requests, interrupts radio and resumes it after dismissal.
- Compressed audio buffering handles short network gaps. Source outages and prolonged Wi-Fi interruptions can still stop playback.

| Screen | Input | Action |
| --- | --- | --- |
| Home | Single UP / DOWN | Volume +5 / -5 (10–100) |
| Home | Double UP / DOWN | Previous / next station |
| Home | OK / hold OK | Play-pause / settings |
| Permission | Single UP / DOWN | Approve / deny |
| Settings | Single UP / DOWN | Navigate |

## Install

Open the [installation page](https://zs20200101.github.io/passport-claude-radio/install/) or follow the [complete companion guide](tools/claude_lan_bridge/README.md). The release ZIP includes verified firmware, the Windows companion source, pinned Python dependencies, licenses and SHA256 checksums. Python 3.10+ is required for the companion; FFmpeg with `libmp3lame` is required for Phoenix. ESP-IDF is needed only to build from source.

For a new installation, use a USB data cable and flash `FoloToy-AI-Passport-full.bin` at **0x0** using the [Passport browser flasher](https://ai-passport.folotoy.cn/tools/web-flasher/). The merged image can reset Wi-Fi and pairing data. Connect the device to a **2.4 GHz Wi-Fi** network on the same LAN as the computer, then configure the companion and pair over USB. Reserve the computer's LAN address in the router. Only enable the optional Claude Code hook after the bridge and pairing work.

For an upgrade from this app with the same partition table, experienced users can write `FoloToy-AI-Passport.bin` at **0x10000** to preserve compatible NVS settings. Never write that app-only file at 0x0. Do not perform a full-chip erase as a routine step.

The companion must stay running for permission prompts, lunar/weather updates and Phoenix transcoding. Other MP3 radio sources connect directly from the device. This integrates Claude **Code** sessions that dispatch the hook; ordinary Claude chat and arbitrary Windows dialogs are outside the integration. Actual Claude Desktop hook dispatch and every radio station remain unverified. Radio/weather services can change or impose regional restrictions.

## Privacy and network

Each installation generates its own private pairing key. No Wi-Fi password, pairing configuration, NVS dump or device logs are distributed. The approval protocol authenticates messages with HMAC-SHA256 but LAN HTTP is not encrypted. Use a trusted private LAN; do not forward the bridge port to the Internet. Location/weather providers receive the network requests and weather coordinates; no Claude prompt is sent to them. IP location can differ from the physical location, especially with VPNs.

## Build and validation

Use ESP-IDF **5.5.3** and run `./tools/validate.sh` (host checks plus isolated firmware build and merged-image verification). See [build instructions](docs/development/engineering/build-and-test.md). Release v1.0.0 reuses the exact firmware accepted on hardware before publication; its embedded project version is `a06235e-dirty`. The source-input manifest and hashes bind it to the release sources; rebuilding at the release commit can change version metadata and binary hashes.

Build: PASS (reused verified build). Host tests: PASS. Device tests: PASS for home controls, display, Phoenix audio and synthetic permission interruption/resume. Unverified: actual Desktop hook dispatch, long-term stability and all stations. Latest physical permission keys were not repeated; earlier physical acceptance is historical evidence.

## Credits and license

Application based on [leo0183/leo-radio](https://github.com/leo0183/leo-radio) and [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport). MIT source licenses: [FoloToy](LICENSE), [leo-radio](LICENSES/leo-radio.txt), [esp-wifi-connect](LICENSES/esp-wifi-connect.txt). Fonts: [Noto Sans SC, SIL OFL 1.1](assets/fonts/OFL.txt). Broadcast content belongs to its providers; this project distributes a player and public source references, not recordings. See [dependency notices](LICENSES/README.md).

The source manifest records separate SHA256 values for the exact Windows build inputs and committed Git bytes; CRLF/LF differences are checked after line-ending normalization.
