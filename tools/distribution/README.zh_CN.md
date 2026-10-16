<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# 安装 Passport Claude Radio

[分享安装页面](https://zs20200101.github.io/passport-claude-radio/install/) · [源码及更新](https://github.com/zs20200101/passport-claude-radio)

以下命令从**发布 ZIP 解压后的根目录**运行，使用 Windows PowerShell 和 Python 3.10 或以上。安装预编译固件不需要 ESP-IDF。请解压到长期保留的目录：可选 hook 会引用这里的 Python 和脚本路径，移动或删除后需要重新安装 hook。

## 烧录与 Wi-Fi

1. 用 USB 数据线连接 FoloToy AI Passport（ESP32-C3／8 MB），关闭占用串口的软件。
2. 在电脑 Chrome／Edge 打开[官方网页烧录工具](https://ai-passport.folotoy.cn/tools/web-flasher/)，选择 `firmware/FoloToy-AI-Passport-full.bin`，从 **0x0** 烧录。完整镜像可能重置 Wi-Fi 和配对设置，不需要全芯片擦除。
3. 手机连接设备的 `LEO Radio` 热点，打开 `http://192.168.4.1`，填写自己的 **2.4 GHz** Wi-Fi。电脑和设备应处于可互通的私有局域网，访客网络和客户端隔离可能阻止连接。

## 电脑端、配对和可选 Claude Code hook

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/claude_lan_bridge/requirements.txt
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py
.\.venv\Scripts\python.exe tools/claude_lan_bridge/bridge.py --config "$env:LOCALAPPDATA\PassportClaude\config.json"
```

保留此窗口运行。凤凰资讯还需要单独安装带 `libmp3lame` 的 FFmpeg，确保桥接进程的 PATH 能找到 `ffmpeg`。包内不捆绑 FFmpeg。其他 MP3 电台直接播放；网络定位天气与农历更新也需要电脑端。

在同一目录另开 PowerShell，把示例 COM5 和 192.168.1.10 替换成实际的 Passport 串口和**电脑局域网 IPv4**：

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --serial COM5 --host 192.168.1.10
```

设备显示已连接后，可以拔掉 USB 无线使用。建议路由器固定电脑 IP，地址变更后重新配对。防火墙阻止时，只对可信私有局域网开放 TCP 8765；安装程序不会自行更改防火墙。

桥接和配对正常后，按需启用 Claude Code 权限控制：

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --install-hook
```

重启 Claude Code 会话。仅支持实际触发 `PermissionRequest` 的 Code 会话，普通聊天及 Windows 弹窗不在范围内。修改 hook 前会备份设置。启用后若设备或桥接离线，请求可能等待后拒绝。恢复原流程时，只从 `~/.claude/settings.json` 的 `hooks.PermissionRequest` 中移除 Passport 条目，保留其他 hook，再重启会话。按 Ctrl+C 停止桥接。

## 按键和升级

主页：单击上／下键调音量（每次 5），双击上／下键换台，确定播放／暂停，长按确定进入设置。权限提示：单击上批准，下拒绝；提示时暂停广播，结束后按原播放状态恢复。

已安装本项目且**分区表一致**时，有经验的用户可以只把 `firmware/FoloToy-AI-Passport.bin` 写到 **0x10000**，保留兼容 NVS。应用文件绝不能写到 0x0；不要将此方法直接套用到无关固件。

## 校验和隐私

用 `Get-FileHash` 对照 `SHA256SUMS.txt`；`release-manifest.json` 记录地址和镜像身份。已实机验收的完整固件 SHA256 为 `71ac40b4a0f10beed7560d90a8c12287d22e14fed459f5d726ce5ec28a48a1ee`。内嵌版本为 `a06235e-dirty`，发布标签为 v1.0.0-claude-radio。

每位用户生成独立私人配置和配对密钥。公开包不包含凭据、NVS、日志和调试文件。HMAC 认证批准消息，局域网 HTTP 不加密，请在可信网络使用，不向公网开放端口。IP 定位可能不准或反映 VPN 出口；广播源和服务也可能失效。实体主页按键、显示、凤凰声音及模拟权限打断恢复已通过，真实 Desktop hook、全部电台和长期稳定性仍待验证。

包含 MIT 应用许可和字体 OFL 声明。广播内容属于对应提供方。完整双语设计和编译说明见[项目仓库](https://github.com/zs20200101/passport-claude-radio)。

源码清单分别记录实机构建输入字节和 Git 提交字节的 SHA256；Windows CRLF 与 Git LF 的差异经过逐文件归一化核对。
