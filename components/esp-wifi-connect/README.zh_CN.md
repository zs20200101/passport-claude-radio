<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# ESP32 Wi-Fi Connect

本组件管理设备的 Wi-Fi 连接。先尝试 NVS 中保存的网络；应用在连接失败后可以启动热点和网页供手机配网。配网页面地址是 `http://192.168.4.1`。

集成来源为 `78/esp-wifi-connect`，版本 3.2.2，提交 `c24b97c194e6b4a1d7be0237b3c28980661cac1e`；上游组件清单声明 MIT 许可。接口与英文使用示例见 [英文文档](README.md)，事件定义见 [wifi_manager.h](include/wifi_manager.h)。

Wi-Fi 凭据保存在 NVS 的 `wifi` 命名空间：`ssid` 到 `ssid9`、`password` 到 `password9`。本应用保留这些数据，NVS 初始化失败时不会自动擦除。

支持保存多个网络、选择同名网络中信号较强的接入点、多语言门户、连接状态回调及可选高级配置。`station_failure_retry_cnt` 控制连接重试次数；`show_ota_config` 和 `show_sleep_config` 默认关闭。本应用不启用 OTA 和深睡配置。

手机首次配网时连接设备热点，选择 2.4 GHz 网络。配网成功后设备退出门户并连接无线网络。原始组件示例中的全量 NVS 擦除代码不适用于需要保留数据的本应用。
