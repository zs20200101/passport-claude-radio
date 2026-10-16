<p align="right"><a href="README.md">English</a></p>

# 依赖与许可说明

- FoloToy AI Passport：根目录 MIT 许可证。
- leo-radio：MIT，见 leo-radio.txt。
- ESP Wi-Fi Connect 3.2.2：MIT，提交 c24b97c194e6b4a1d7be0237b3c28980661cac1e。来源元数据保存在 components/esp-wifi-connect/idf_component.yml，许可文本见 esp-wifi-connect.txt。上游源码没有本地功能修改；增加了集成所需的双语说明。
- Noto Sans SC：SIL OFL 1.1，见 assets/fonts/OFL.txt。
- lunar-python 1.4.8：MIT；pyserial 3.5：BSD。安装依赖时会带入对应 wheel 的许可文件；发布包不捆绑它们的代码。
- FFmpeg 由用户单独安装，不随包分发；适用许可证取决于用户选择的构建。

公开包排除 ELF／MAP 调试文件、私人配置、设备日志和 NVS 读回数据。

`firmware/` 保留 ESP-IDF 5.5.3 及托管依赖的固件再分发声明。特别是 esp_audio_codec 使用 Espressif Modified MIT License，**仅可与乐鑫产品配合使用**；应用的 MIT 许可证不覆盖此限制。本固件仅面向 ESP32-C3。
