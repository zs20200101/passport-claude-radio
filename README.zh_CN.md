<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# Passport Claude Radio

为 **FoloToy AI Passport（ESP32-C3，8 MB Flash）** 制作的广播、时钟与 Claude Code 权限遥控器。

[分享安装页面](https://zs20200101.github.io/passport-claude-radio/install/) · [下载发布版本](https://github.com/zs20200101/passport-claude-radio/releases/latest) · [English](README.md)

- 加粗显示时间、日期、离线农历，以及根据电脑网络定位的城市天气。
- 保留昆明等城市电台、央广、广东，增加凤凰资讯；已删除香港和故事 FM。
- 局域网签名桥接转发 Claude Code 权限请求；提示时打断广播，提示结束后恢复。
- 压缩音频缓冲用于吸收短暂网络波动；长时间断网或音源故障仍可能中断播放。

| 界面 | 操作 | 功能 |
| --- | --- | --- |
| 主页 | 单击上／下键 | 音量加／减 5（范围 10–100） |
| 主页 | 双击上／下键 | 上一台／下一台 |
| 主页 | 确定／长按确定 | 播放暂停／设置 |
| 权限提示 | 单击上／下键 | 批准／拒绝 |
| 设置 | 单击上／下键 | 移动选项 |

## 安装

打开[分享安装页面](https://zs20200101.github.io/passport-claude-radio/install/)，或阅读[完整电脑端指南](tools/claude_lan_bridge/README.zh_CN.md)。发布 ZIP 包含已验证固件、Windows 电脑端源码、固定版本的 Python 依赖清单、许可证和 SHA256 校验文件。电脑端需要 Python 3.10 或以上；凤凰资讯需要带 `libmp3lame` 的 FFmpeg。只有自行编译才需要 ESP-IDF。

首次安装：用 USB 数据线连接设备，在 [Passport 网页烧录工具](https://ai-passport.folotoy.cn/tools/web-flasher/)选择 `FoloToy-AI-Passport-full.bin`，从 **0x0** 烧录。完整镜像可能重置 Wi-Fi 和配对设置。设备连接与电脑同一局域网的 **2.4 GHz Wi-Fi**，配置电脑端并通过 USB 配对。建议在路由器固定电脑的局域网 IP。桥接和配对成功后，再启用可选的 Claude Code hook。

已安装本项目且分区表一致的用户，可使用专业烧录工具只把 `FoloToy-AI-Passport.bin` 写入 **0x10000**，保留兼容的 NVS 配置。应用文件不能写到 0x0。不要将全芯片擦除作为日常安装步骤。

权限提示、农历天气更新和凤凰转码需要电脑桥接持续运行，其他 MP3 电台由设备直接连接。仅集成能够触发 hook 的 Claude **Code 会话**；普通 Claude 聊天和任意 Windows 弹窗不在集成范围。真实 Claude Desktop hook 分发及全部电台尚未逐项验证。广播和天气服务可能变化或受地区限制。

## 隐私与网络

每位安装者独立生成配对密钥；公开包不包含 Wi-Fi 密码、私人配对配置、NVS 备份或设备日志。批准协议通过 HMAC-SHA256 验证消息，但局域网 HTTP 不加密。请在可信私有局域网使用，不要向互联网映射桥接端口。定位和天气服务会接收网络请求及天气坐标，不会收到 Claude 提示内容。IP 定位与实际位置可能不同，尤其在使用 VPN 时。

## 编译与验证

使用 ESP-IDF **5.5.3**，运行 `./tools/validate.sh`（电脑测试、隔离固件构建、合并镜像验证），见[构建说明](docs/development/engineering/build-and-test.zh_CN.md)。v1.0.0 复用公开前已实机验收的同一固件，内嵌项目版本为 `a06235e-dirty`；源码输入清单及哈希将其绑定到发布源码。在发布提交重新构建可能因版本元数据不同而产生不同哈希。

Build：PASS（复用已验证构建）。Host tests：PASS。Device tests：PASS，覆盖主页按键、显示、凤凰声音及模拟权限打断恢复。Unverified：真实 Desktop hook、长期稳定性及全部电台。最新版没有重做实体权限按键测试；之前实体按键验收保留为历史证据。

## 来源与许可证

基于 [leo0183/leo-radio](https://github.com/leo0183/leo-radio) 和 [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport)。MIT 源码许可：[FoloToy](LICENSE)、[leo-radio](LICENSES/leo-radio.txt)、[esp-wifi-connect](LICENSES/esp-wifi-connect.txt)。字体使用 [Noto Sans SC，SIL OFL 1.1](assets/fonts/OFL.txt)。节目内容属于对应提供方；本项目发布播放器和公开源地址，不分发节目录音。见[依赖说明](LICENSES/README.zh_CN.md)。
