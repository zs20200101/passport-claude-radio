<p align="right"><a href="README.zh_CN.md">简体中文</a></p>

# Dependency notices

- FoloToy AI Passport: root MIT license.
- leo-radio: MIT, see leo-radio.txt.
- ESP Wi-Fi Connect 3.2.2: MIT, commit c24b97c194e6b4a1d7be0237b3c28980661cac1e. Upstream metadata is in components/esp-wifi-connect/idf_component.yml; license notice is esp-wifi-connect.txt. Upstream source has no local functional changes; paired documentation was added for integration.
- Noto Sans SC: SIL OFL 1.1, assets/fonts/OFL.txt.
- lunar-python 1.4.8: MIT. pyserial 3.5: BSD. Their wheel license files accompany installed dependencies; the release does not bundle their code.
- FFmpeg is installed separately and is not bundled; its applicable license depends on the build chosen by the user.

The public release excludes ELF/MAP debug files, private configuration, device logs and NVS readbacks.

Firmware redistribution notices for ESP-IDF 5.5.3 and managed dependencies are retained under `firmware/`. In particular, esp_audio_codec uses the Espressif Modified MIT License and may be used **exclusively with Espressif products**; the application MIT license does not override that condition. The shipped firmware targets ESP32-C3 only.
