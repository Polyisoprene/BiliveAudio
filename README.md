# BiliveAudio

Bilibili 直播音频客户端 — 纯音频播放，CPU/内存占用极低，适合后台挂机听直播。

## 原理

### 整体流程

```
Bilibili API ─→ LiveMonitor ─→ 开播列表
     │                              │
     │                              ▼ 选择房间
     ▼                          getRoomInfo()
  AuthManager ─→ BilibiliApi ─────────→ getStreamUrl()
     │                                      │
     │                                      ▼
     │                                  StreamPlayer (mpv)
     │                                      │
     ▼                                      ▼
 DanmakuManager ─────────WebSocket────────→ DanmakuPanel / DanmakuWindow
```

### 核心模块

#### BilibiliApi — HTTP 客户端

所有 API 请求经同一 `QNetworkAccessManager` 发出，自动携带 cookie 和 `User-Agent`。关键请求需要 **w_rid/wts 签名**（Bilibili 的新版防爬机制）：

1. 从 `nav` 接口获取 `wbi_img` 的 `img_url`、`sub_url`
2. 提取文件名中的 key，按固定顺序排列后取 MD5 前 32 位作为 mixin key
3. 请求参数排序后拼接 mixin key 再 MD5，得到 `w_rid` 参数

#### AuthManager — 认证

使用 **QR 码扫码登录**：

1. `fetchQRCode()` 获取二维码 URL 和 `qrcode_key`
2. 轮询 `pollQRCode()` 等待用户扫码
3. 扫码确认后，从回调 URL 的 query 参数提取 `bili_jct`（CSRF token）和 `DedeUserID`（UID）
4. cookie 持久化到 `QSettings`

#### LiveMonitor — 开播检测

**单请求设计**（替代原先的分页关注列表 + 批量状态查询）：

- 每隔 30s 调用 `GET /x/polymer/web-dynamic/v1/portal?up_list_more=1`
- 直接解析 `data.live_users.items` 数组，返回正在直播的关注 UP 主
- 对比上一次结果，检测新开播并触发通知

#### StreamPlayer — 音频播放

基于 **libmpv**，配置 `vo=null` `video=no` 完全禁掉视频解码，只请求 FLV 的音频轨道：

- 设置 `referrer` 和 `user-agent` 通过 CDN 鉴权
- `stream-lavf-o` 启用自动重连（reconnect）
- `cache-secs=5` 缓冲 5 秒抗网络抖动
- 暂停后 `resume()` 执行 `seek 100 absolute-percent` 跳到直播实时位置

**Windows 部署**：mpv 的 DLL 和头文件来自 NuGet 包 `Endpne.LibMPV.Windows`，CI 自动拉取。

#### DanmakuManager — 弹幕

通过 Bilibili 弹幕 WebSocket 接收实时弹幕：

1. 先调用 `getDanmuInfo` 获取 WebSocket 地址列表和 `token`
2. 随机选择一个主机建立 `wss://` 连接
3. 发送认证包（`protover=1`，JSON 格式含 `uid`、`roomid`、`key`、`buvid`）
4. 服务端返回心跳间隔后，定时发送心跳维持连接
5. 弹幕数据以 **Brotli 压缩**传输（protover=3），用 `libbrotlidec` 解压
6. 断线自动重连（随机换主机，最多 3 次）

#### 弹幕渲染 — DanmakuBubble

每条弹幕渲染为一个圆角气泡：

- 头像：通过 `BilibiliApi::fetchUserFace` 按 UID 下载并缓存
- 粉丝勋章：从弹幕数据 `info[3]` 解析勋章名 + 等级 + 颜色
- Super Chat：金色背景 `#FFB800`
- 礼物：粉色背景 `#FF69B4`
- 文本颜色根据背景色自动选择黑/白确保 WCAG 对比度

### 音频-only 说明

mpv 配置 `video=no` 让 libavformat 在解复用 FLV 时跳过视频包，CPU/GPU 完全不参与视频解码。CDN 仍然发送完整的 FLV 流（无法节省带宽），但本地解码开销几乎为零。

## 构建

### 依赖

| 平台 | 依赖 |
|------|------|
| Linux | `qt6-base-dev qt6-websockets-dev libmpv-dev libspdlog-dev libqrencode-dev libbrotli-dev libgl-dev cmake g++` |
| Windows | vcpkg 管理 `spdlog` `libqrencode` `brotli`；mpv 来自 NuGet（CI 自动下载） |

### 构建命令

```bash
# Linux
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Windows (MSVC + vcpkg)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# 或者用 Meson（仅 Linux）
meson setup build-meson -Dbuildtype=release
ninja -C build-meson
```

## 配置

配置文件位置：
- **Linux**: `~/.config/BiliveAudio/biliveaudio.conf`
- **Windows**: `%LOCALAPPDATA%/BiliveAudio/biliveaudio.conf`

日志文件：`~/.biliveaudio/logs/app.log`（每日轮转，保留 7 天，可在设置界面调整）

## 技术栈

| 模块 | 技术 |
|------|------|
| UI 框架 | Qt 6 (Widgets, WebSockets, Network) |
| 音频后端 | libmpv（`video=no`, `vo=null`） |
| 日志 | spdlog（异步，每日文件轮转） |
| 二维码 | libqrencode |
| 压缩 | Brotli（弹幕 WebSocket protover=3） |
| API 签名 | w_rid/wts（Bilibili WBI 签名） |

## 架构

```
src/
├── core/                      # 核心逻辑
│   ├── BilibiliApi.cpp/h      HTTP API 客户端（含 w_rid 签名）
│   ├── AuthManager.cpp/h      QR 码扫码登录 / 会话恢复
│   ├── LiveMonitor.cpp/h      动态门户 API 轮询开播状态
│   ├── StreamPlayer.cpp/h     mpv 音频播放器封装
│   └── DanmakuManager.cpp/h   WebSocket 弹幕（Brotli 解压）
├── ui/                        # 界面组件
│   ├── MainWindow.cpp/h       主窗口
│   ├── LoginDialog.cpp/h      QR 码扫码弹窗
│   ├── LiveListWidget.cpp/h   关注直播列表
│   ├── PlayerControl.cpp/h    播放/暂停/音量控制
│   ├── DanmakuBubble.cpp/h    弹幕气泡（圆角、头像、勋章、SC、礼物）
│   ├── DanmakuPanel.cpp/h     嵌入式弹幕面板
│   ├── DanmakuWindow.cpp/h    独立浮动弹幕窗（可调透明度、拖拽）
│   ├── SettingsDialog.cpp/h   设置窗口
│   └── TrayManager.cpp/h      系统托盘
├── models/                    # 数据结构
│   ├── UserInfo.h
│   ├── LiveRoom.h
│   ├── FollowedUser.h
│   └── Danmaku.h
└── utils/                     # 工具
    ├── Logger.cpp/h           spdlog 封装（可配置路径和保留天数）
    └── Settings.cpp/h         QSettings 封装
```

## License

MIT
