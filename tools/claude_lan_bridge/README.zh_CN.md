<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# Claude 局域网电台伴侣

Passport 待机显示时间、日期、农历、缓存天气和城市电台。Claude Code 出现 `PermissionRequest` 时暂停音频并显示独立覆盖层：**上键批准，下键拒绝**。决定提交、超时或连接中断后关闭提示；只有用户和定时器仍要求播放时才恢复电台。定时器只暂停音频，保留 Wi-Fi 与权限处理。

## 配置电脑

使用 Python 3.10 及以上与 ESP-IDF **5.5.3**。在仓库根目录的 PowerShell 中运行：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/claude_lan_bridge/requirements.txt
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py
.\.venv\Scripts\python.exe tools/claude_lan_bridge/bridge.py --config "$env:LOCALAPPDATA\PassportClaude\config.json"
```

先启动桥接并完成设备配对。确认设备在线后，再运行 `setup.py --install-hook` 启用全局权限 hook；提前启用会使尚未配对的请求等待并拒绝。

配置程序在上述私有文件中生成 64 位十六进制令牌，将目录 ACL 限制为当前用户和 SYSTEM。添加 `--install-hook` 时才向 `~/.claude/settings.json` 合并本项目的 `PermissionRequest` hook。修改前会在设置文件旁备份；重复运行保留令牌、不重复添加 hook。采用当前 Claude Code 的可执行文件加参数格式，支持中文和空格路径。安装后重启 Code 会话。桥接服务停止时，该 hook 会拒绝请求；如需恢复原来的权限处理，只移除 Passport 对应的 hook 条目。

集成面向 Claude **Code 会话**，包括会加载这些本地 hook 的桌面版 Code 会话。普通 Claude 聊天及任意 Windows 权限窗口不属于本集成。已安装桌面版是否实际加载 hook、沙箱／网络权限是否触发对应事件，需要实测。已有自动允许规则可能使某些操作不出现请求。配置程序不登录 CLI、不修改账号或权限允许列表。

## 经授权烧录后配对设备

1. 首次启动用手机连接 `LEO Radio` 热点；门户未自动打开时访问 `http://192.168.4.1`。选择与电脑同一局域网的 2.4 GHz Wi-Fi。兼容的分段烧录可保留已有网络设置。
2. 确认目标 Passport 的 USB 串口和电脑局域网 IPv4 地址，用物理 USB 配对：

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --serial COM5 --host 192.168.1.10
```

以上串口与地址是示例。该命令只原子写入一份 NVS 配对数据，不烧录。收到成功提示后才能拔线。建议在路由器设置 DHCP 地址保留；电脑地址改变后重新配对。无线客户端隔离或 Windows 防火墙可能阻止 TCP 8765；需要时只允许指定私有局域网访问，本项目不自动修改防火墙。

签名轮询成功后显示“Claude 已连接”。提示出现会唤醒并保持屏幕亮起，期间其他按钮动作被消耗。提示出现前已按下的按键不能批准新请求。长命令滚动显示，但摘要有长度限制；重要操作先在电脑查看全文。USB 只用于初次烧录／配对，无线使用期间电脑桥接服务须保持运行。

**广播分类**保留原有城市（含昆明）、央广和广东，新增**凤凰资讯**，移除香港和在线听书。原城市、央广、广东的 0–10 编号不变；旧版停在香港或听书时，通过目录版本标记迁移到昆明。主页单击上／下键增大／减小音量，每次 5 格，范围 10–100；双击上／下键切换上一台／下一台。频道编号旁显示当前音量。确定键暂停／继续，长按确定进入设置。设置页仍用单击上／下导航。权限提示仍是单击上键批准、单击下键拒绝，广播手势不会操作权限提示。

凤凰资讯使用凤凰网域名下公开的 HLS/AAC 纯音频源。已配对的电脑通过 FFmpeg 将这一固定音源转换为 64 kbit/s 单声道 MP3，在原桥接端口提供 `/radio/phoenix.mp3`。电脑需要安装含 `libmp3lame` 的 FFmpeg，并让桥接进程能在 PATH 中找到 `ffmpeg`。收听凤凰资讯时电脑和桥接服务须保持运行；其余 MP3 电台仍由设备直连。公开音频接口仅允许私网／本机访问，最多两个听众，不运行 shell，不接受自定义网址；连接断开或输出停滞时会回收转码进程。此接口不提供配对凭据或权限控制，原有签名批准协议保持不变。

下载和解码分为两个任务，使用 32 KiB 压缩音频环形缓冲，首次播放和缓冲耗尽后预存 24 KiB。在 64 kbit/s 下约容纳四秒音频，预存约 3 秒。播放阶段单次读取最多等待 700 毫秒，使权限提示能在 1.5 秒停止期限内打断音频；临时读取超时最多重试八秒，连接失效时先播放已有缓冲再重连。音量动画最多每秒刷新十次。此优化可吸收短时延迟，但无法保证长时间 Wi-Fi／音源中断时仍连续播放。原有连接后的性能模式仍关闭 Wi-Fi 调制解调器省电。


保留电台中文原名，并按音频流 URL 去重。物理 USB 也支持发送 `PASSPORT_CITY <选项>` 加换行，使用完整中文城市／分类名；`PASSPORT_CITY_QUEUED` 表示独立操作已进入保存／搜索队列，未知选项或队列已满返回 `PASSPORT_CITY_ERROR`。此命令不能注入按键或批准 Claude 提示。天气城市仍独立自动定位。

## 天气与日历

天气默认根据电脑当前接入网络的出口自动定位。中国网络优先通过直连 myip.ipip.net 获取国内城市（绕过应用 HTTP 代理），再用 Open-Meteo 地理编码按城市和省份匹配坐标。首次国内接口不可用或返回其他国家时，回退到 ipwho.is。已获得国内城市后，国内接口故障会保留原城市并标记定位缓存，避免悄悄切换为冲突的全球 IP 定位结果。城市和天气每 15 分钟刷新；地理编码失败也保留原定位并重试。请求不包含 Claude 提示、对话或配对令牌，也不会将查询到的公网 IP 传给地理编码服务。数据库或 VPN 出口仍可能与实际所在地不同，这是网络定位，并非 GPS。

将已有配置切换回自动定位：

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --auto-location
```

如需手动覆盖，同时提供选定城市与经纬度：

```powershell
.\.venv\Scripts\python.exe tools/claude_lan_bridge/setup.py --city CITY --latitude LAT --longitude LON
```

修改后重启桥接。电脑通过 HTTPS 从 Open-Meteo 获取天气，每 15 分钟更新；失败时保留上次结果并标记缓存。坐标会发送给该服务。电台城市与天气城市分别设置。农历通过 `lunar-python` 离线计算；时间使用 Asia/Shanghai（UTC+8），由电脑同步与设备 SNTP 校准。电脑数据长时间不更新时，设备标记农历／天气陈旧。电台目录与直播依赖上游公共服务及台源可用性，仅启用 MP3 解码。

## 协议和失败行为

v2 协议使用 HMAC-SHA256 对 `METHOD + LF + PATH + LF + nonce + LF + body` 签名；nonce 为 16 个随机字节的小写十六进制形式，共 32 字符。请求头是 `X-Passport-Nonce`、`X-Claude-Bridge-Signature`。响应头 `X-Passport-Response` 签名内容为 `nonce + LF + decimal_status + LF + body`。密钥使用 64 位十六进制令牌的 UTF-8 字节，不将其解码为二进制。

`/hook` 只接受本机回环地址、拒绝浏览器 Origin，并要求认证。只转发工具名（最多 32 UTF-8 字节）、摘要（256）和工作目录（160），不发送会话记录；实际命令／路径优先于描述。设备接口为 `/v1/pending`、`/v1/decision`、`/v1/idle`。决定必须匹配已投递的请求 ID 和一次性随机 challenge；拒绝过期、重复及未投递的批准。请求最多等待 120 秒；投递后通过五秒轮询租约检测断线。队列最多八个请求，失败、超时或队满均拒绝。若决定已被电脑接受但响应丢失，设备可能无法确认最终状态，应检查 Code 会话。

HTTP 内容有认证但**没有加密**，局域网观察者可读取上述有限元数据。仅在可信局域网使用，不向互联网转发端口，不公开私有配置。无签名的 `/health` 只暴露协议版本。重新配对会原子更新配置，并拒绝旧配对正在处理的请求。

## 验证与构建

```bash
PYTHON=.venv/Scripts/python.exe ./tools/validate.sh --static
./tools/validate.sh
```

Windows 先激活已安装的 ESP-IDF PowerShell 环境，再从该环境调用 Git Bash 运行门禁。Windows 固件阶段会在 `C:/ai-passport-validation` 下创建隔离的 ASCII 路径副本，绕开 ESP-IDF Kconfig 对中文项目路径的故障。使用正式默认配置、保留源目录和本地 sdkconfig，验证合并镜像，归档匹配的 ELF／MAP／分段镜像，并记录源码哈希。阶段结束后移除临时副本。合并固件位于 `build/FoloToy-AI-Passport-full.bin`，从 **0x0** 烧录，可能重置持久数据；不能把单独应用镜像当作全量镜像。

主机测试覆盖真实 HTTP 和 hook 子进程、认证／篡改／重放、队列／超时／断线、一次性决定、设置合并／备份、农历样例、天气缓存失败、独立状态机与字库覆盖。这些不证明按键电气行为、中文字形实际显示、音频、运行内存余量、Wi-Fi 配网或桌面版事件分发正常；这些需要另行授权的实机测试。

## 来源和许可

- 电台应用与标题字库：[leo-radio](https://github.com/leo0183/leo-radio)，MIT，版权声明保留于 [leo-radio.txt](../../LICENSES/leo-radio.txt)。本构建不包含上游可选视频／彩蛋资源。
- Wi-Fi 组件：`78/esp-wifi-connect` 3.2.2，清单声明 MIT，固定源码提交见组件清单。
- Noto Sans SC 及生成正文词库：SIL OFL 1.1，[许可](../../assets/fonts/OFL.txt)，来源／生成方式见 [assets](../../assets/README.zh_CN.md)。
- `lunar-python` 1.4.8：MIT；`pyserial` 3.5：BSD；版本固定于 `requirements.txt`。
- 实现参考用户指定的 Passport Agent 教程及 Anthropic 当前 Claude Code hook 文档。升级 Claude 时应核对 `PermissionRequest` 返回 JSON 与 hook 可执行文件／参数支持。
