# BiliveAudio

Bilibili 直播音频客户端 — 纯音频播放，CPU/内存占用极低，适合后台挂机听直播。

## 原理

### 整体流程

```
Bilibili API ─→ LiveMonitor ─→ 开播列表
     │                              │
     │                              ▼ 选择房间
  AuthManager ─→ BilibiliApi ─────────→ getRoomInfo() → getStreamUrl()
     │                                  │
     │                                  ▼
     │                     StreamPlayer (libmpv 纯音频)
     │                                  │
     ▼                                  ▼
 DanmakuManager ─────────WebSocket────→ DanmakuPanel / DanmakuBubble
```

### 核心模块

#### BilibiliApi — HTTP 客户端

所有 API 请求经同一 `QNetworkAccessManager` 发出，自动携带 cookie 和 `User-Agent`。关键请求需要 **w_rid/wts 签名**（Bilibili 的新版防爬机制）：

1. 从 `nav` 接口获取 `wbi_img` 的 `img_url`、`sub_url`
2. 提取文件名中的 key，按固定顺序排列后取 MD5 前 32 位作为 mixin key
3. 请求参数排序后拼接 mixin key 再 MD5，得到 `w_rid` 参数

主要接口：

- `getUserInfo` — `nav`，登录态检测 + 提取 wbi key + 合并 Set-Cookie
- `fetchLiveFollowed` — 动态门户 `portal`，单请求返回正在直播的关注列表
- `getRoomInfo` / `getStreamUrl` — 房间信息与直播流地址（`playUrl`）
- `getDanmuInfo` — 弹幕 WebSocket 服务器列表 + token（专用 NAM，禁用 HTTP/2 避免多 Set-Cookie 被合并）
- `sendLiveDanmaku` — 发送弹幕（需 CSRF token）
- `fetchUserFace` — 头像 URL 获取（失败自动指数退避重试，最多 3 次）

#### AuthManager — 认证

使用 **QR 码扫码登录**：

1. `fetchQRCode()` 获取二维码 URL 和 `qrcode_key`
2. 轮询 `pollQRCode()` 等待用户扫码
3. 扫码确认后分两种流程换取 Cookie：
   - **新版**：回调 URL 只携带一次性 `ticket`，需请求 crossDomain 链接，手动跟随重定向链，逐跳收集响应的 `Set-Cookie` 头（禁用自动重定向和 HTTP/2，否则会丢失中间的 Cookie）
   - **旧版回退**：Cookie 直接以 querystring 参数拼接在回调 URL 中
4. cookie 持久化到 `QSettings`

#### LiveMonitor — 开播检测

**单请求设计**（替代原先的分页关注列表 + 批量状态查询）：

- 每隔 30s 调用 `GET /x/polymer/web-dynamic/v1/portal?up_list_more=1`
- 直接解析 `data.live_users.items` 数组，返回正在直播的关注 UP 主
- 对比上一次结果，检测新开播并触发通知

#### StreamPlayer — 音频播放

基于 **libmpv** 的封装，以纯音频模式播放 FLV 直播流：

- `vo=null` + `video=no` — 完全不解码视频帧
- `reconnect=1` / `reconnect_streamed=1` — 断流自动重连
- 设置 `referrer` 和 `user-agent` 通过 CDN 鉴权
- `demuxer-max-bytes` / `demuxer-max-back-bytes` — 限制缓冲占用
- mpv 事件通过 `mpv_set_wakeup_callback` 派发到主线程事件循环处理（`END_FILE` / `eof-reached` / `demuxer-cache-state`），无额外线程
- 暂停/恢复直接设置 mpv `pause` 属性，暂停期间网络连接保持
- 音量 0–100，持久化到设置

#### DanmakuManager — 弹幕

通过 Bilibili 弹幕 WebSocket 接收实时弹幕：

1. 先调用 `getDanmuInfo` 获取 WebSocket 地址列表和 `token`
2. 随机选择一个主机建立 `wss://` 连接
3. 发送认证包（握手头 `protover=1`，JSON 体内声明 `protover=3` 表示接受 Brotli 压缩，内容含 `uid`、`roomid`、`key`、`buvid`）
4. 认证成功后每 10s 发送心跳维持连接，并解析观众数
5. 弹幕数据以 **Brotli 压缩**传输（protover=3），用 `libbrotlidec` 解压
6. 断线自动重连（随机换主机，最多 3 次）
7. 支持发送弹幕（走 REST API，需登录）

#### AppController — 装配层

串联以上模块：会话恢复、房间打开/关闭状态机、播放/暂停/音量、错误与日志转发、直播列表与开播通知。

#### 弹幕渲染 — DanmakuBubble

每条弹幕渲染为一个圆角气泡：

- 头像：通过 `BilibiliApi::fetchUserFace` 按 UID 获取并缓存（内存 LRU + 磁盘缓存，webp 自动回退 jpg）
- 粉丝勋章：从弹幕数据 `info[3]` 解析勋章名 + 等级 + 颜色
- Super Chat：金色背景 `#FFB800`
- 礼物：粉色背景 `#FF69B4`
- 文本颜色根据背景色自动选择黑/白确保 WCAG 对比度

### 音频-only 说明

libmpv 以 `vo=null` + `video=no` 运行，不解码任何视频帧，本地解码开销几乎为零。CDN 仍然发送完整的 FLV 流（无法节省带宽），但 CPU/内存占用远低于普通播放器，适合后台挂机。

## 构建

### 依赖

| 平台 | 依赖 |
|------|------|
| Linux | `qt6-base-dev qt6-websockets-dev libmpv-dev libspdlog-dev libqrencode-dev libbrotli-dev libjemalloc-dev libgl-dev cmake g++`（另需 `libgtest-dev` 构建测试） |
| Windows | vcpkg 管理 `spdlog` `libqrencode` `brotli`；mpv 从 NuGet 获取（`Endpne.LibMPV.Windows`，提供 `libmpv-2.dll` 和头文件） |

### 构建命令

```bash
# Linux
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Windows (MSVC + vcpkg，mpv 从 NuGet 解压后指定路径)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake ^
    -DMPV_INCLUDE_DIR=<mpv>/build/x64/include ^
    -DMPV_LIBRARY_RELEASE=<mpv>/build/x64/libmpv-2.lib
cmake --build build --config Release

# 或者用 Meson（仅 Linux）
meson setup build-meson -Dbuildtype=release
ninja -C build-meson
```

### 测试

Google Test 单元测试（数据模型、设置持久化、API 签名/解析、播放器状态、内存边界与泄漏诊断）：

```bash
cmake -S . -B build-tests -DBUILD_TESTS=ON
cmake --build build-tests -j$(nproc)
ctest --test-dir build-tests
```

### 打包

- **Linux**：AppImage（`packaging/linux/appimage.sh`，基于 linuxdeploy + plugin-qt）
- **Windows**：`windeployqt` 收集 Qt 运行时 + mpv DLL 后打包 zip
- 打 `v*` tag 推送即触发 CI（`.github/workflows/release.yml`）构建并上传两个平台的产物

## 配置

- 配置文件（QSettings INI）：位于系统配置目录（Linux 一般为 `~/.config/BiliveAudio/BiliveAudio/biliveaudio.conf`，Windows 为 `%APPDATA%/BiliveAudio/...`），可在设置窗口查看实际路径
- 日志文件：spdlog 异步 + 每日轮转，默认目录为系统应用数据目录（Linux 一般为 `~/.local/share/BiliveAudio/BiliveAudio/logs`），保留天数默认 7 天，路径和保留天数可在设置界面调整（重启后生效）

## 技术栈

| 模块 | 技术 |
|------|------|
| UI 框架 | Qt 6 (Widgets, WebSockets, Network) |
| 播放内核 | libmpv（`vo=null` + `video=no` 纯音频模式） |
| 日志 | spdlog（异步，每日文件轮转） |
| 二维码 | libqrencode |
| 压缩 | Brotli（弹幕 WebSocket protover=3） |
| API 签名 | w_rid/wts（Bilibili WBI 签名） |
| 内存优化 | jemalloc（Linux）+ 定时 `malloc_trim` 整理堆碎片 |

## 架构

```
src/
├── core/                      # 核心逻辑
│   ├── AppController.cpp/h    装配层：信号编排、会话恢复、房间/播放状态机
│   ├── BilibiliApi.cpp/h      HTTP API 客户端（含 w_rid 签名）
│   ├── AuthManager.cpp/h      QR 码扫码登录 / 会话恢复
│   ├── LiveMonitor.cpp/h      动态门户 API 轮询开播状态
│   ├── StreamPlayer.cpp/h     libmpv 纯音频封装
│   └── DanmakuManager.cpp/h   WebSocket 弹幕（Brotli 解压）
├── ui/                        # 界面组件
│   ├── MainWindow.cpp/h       主窗口
│   ├── LoginDialog.cpp/h      QR 码扫码弹窗
│   ├── LiveListWidget.cpp/h   关注直播列表
│   ├── PlayerControl.cpp/h    播放/暂停/音量控制
│   ├── DanmakuBubble.cpp/h    弹幕气泡（圆角、头像、勋章、SC、礼物）
│   ├── DanmakuPanel.cpp/h     嵌入式弹幕面板
│   ├── DanmakuWindow.cpp/h    独立浮动弹幕窗（开发中，未接入构建）
│   ├── SettingsDialog.cpp/h   设置窗口
│   └── TrayManager.cpp/h      系统托盘
├── models/                    # 数据结构
│   ├── UserInfo.h
│   ├── LiveRoom.h
│   ├── FollowedUser.h
│   └── Danmaku.h
└── utils/                     # 工具
    ├── Logger.h               spdlog 封装（header-only，可配置路径和保留天数）
    └── Settings.cpp/h         QSettings 封装
```

其他行为：

- `QApplication::setQuitOnLastWindowClosed(false)` — 关闭主窗口后驻留系统托盘，从托盘退出
- 深色主题来自 `resources/style.qss`
- 主窗口关闭时保存窗口几何信息

## License

MIT
