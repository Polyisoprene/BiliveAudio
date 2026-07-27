# BiliveAudio

Bilibili 直播音频客户端 — CPU/内存占用极低。

## 功能

- Bilibili 扫码登录 / 会话恢复
- 输入房间号或从关注列表打开直播间
- **纯音频播放**（mpv 后端，支持 FLV/HLS），CPU < 2%
- 实时弹幕接收（WebSocket，Brotli 解压）
- 发送弹幕
- 浮动弹幕窗（独立窗口，可调透明度）
- 系统托盘最小化
- 开播提醒通知

## 安装

### Linux (Ubuntu 24.04)

```bash
# 依赖
sudo apt install qt6-base-dev qt6-websockets-dev libmpv-dev \
  libspdlog-dev libqrencode-dev libbrotli-dev libgl-dev cmake g++

# 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 运行
./build/BiliveAudio
```

### Windows

```bash
# 依赖通过 vcpkg 管理
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Arch Linux

```bash
sudo pacman -S qt6-base qt6-websockets mpv spdlog qrencode brotli cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 配置

设置文件位置：
- **Linux**: `~/.config/BiliveAudio/biliveaudio.conf`
- **Windows**: `%LOCALAPPDATA%/BiliveAudio/biliveaudio.conf`
- **macOS**: `~/Library/Preferences/BiliveAudio/biliveaudio.conf`

日志文件：`~/.biliveaudio/logs/app.log`（每日轮转，保留 7 天）

## 快捷键

| 操作 | 快捷键 |
|------|--------|
| 暂停/播放 | 点击播放按钮 |
| 停止播放 | 点击停止按钮 |
| 调整音量 | 拖动音量滑块 |
| 最小化到托盘 | 关闭窗口 |

## 构建选项

```cmake
-DCMAKE_BUILD_TYPE=Release    # 发布版本
-DCMAKE_BUILD_TYPE=Debug      # 调试版本
```

## 技术栈

- **UI**: Qt 6 (Widgets, WebSockets, Network)
- **音频**: libmpv
- **日志**: spdlog (异步，每日文件)
- **二维码**: libqrencode
- **压缩**: Brotli
- **HTTP 签名**: w_rid (Bilibili 新版反爬)

## 架构

```
src/
├── core/        # 核心逻辑
│   ├── BilibiliApi      HTTP API 客户端
│   ├── AuthManager      登录认证
│   ├── LiveMonitor      关注列表轮询
│   ├── StreamPlayer     mpv 播放器封装
│   └── DanmakuManager   WebSocket 弹幕客户端
├── ui/          # 界面组件
│   ├── MainWindow       主窗口
│   ├── LoginDialog      QR 码登录
│   ├── LiveListWidget   关注列表
│   ├── PlayerControl    播放/暂停/停止控制
│   ├── DanmakuPanel     嵌入式弹幕面板
│   ├── DanmakuWindow    浮动弹幕窗
│   └── TrayManager      系统托盘
├── models/      # 数据结构
│   ├── UserInfo
│   ├── LiveRoom
│   ├── FollowedUser
│   └── Danmaku
└── utils/       # 工具
    ├── Logger          spdlog 封装
    └── Settings        QSettings 封装 (INI 文件)
```

## License

MIT
