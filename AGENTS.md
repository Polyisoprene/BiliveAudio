# BiliveAudio — agent guide

CMake + Qt6 + mpv desktop app for listening to Bilibili live streams (audio-only).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Linux deps (Ubuntu 24.04): `qt6-base-dev qt6-websockets-dev libmpv-dev libspdlog-dev libqrencode-dev libbrotli-dev libgl-dev cmake g++`

## Architecture

| Directory | Purpose |
|-----------|---------|
| `src/core/` | BilibiliApi (HTTP), AuthManager (login/cookie), LiveMonitor (poll followed streams), StreamPlayer (mpv wrapper), DanmakuManager (WebSocket) |
| `src/ui/` | MainWindow, LoginDialog (QR code), LiveListWidget, PlayerControl, TrayManager, DanmakuPanel |
| `src/models/` | Plain data structs: UserInfo, LiveRoom, FollowedUser, Danmaku |
| `src/utils/` | Logger (spdlog async, daily files), Settings (QSettings) |

Entrypoint: `src/main.cpp:6`

## Important details

- **No tests, no linter, no typecheck** — no test framework or linting config exists.
- Logs go to `~/.biliveaudio/logs/app.log` (daily rotation, 7-day retention).
- Settings persist via QSettings (cookie, volume, window geometry, last room ID).
- `QApplication::setQuitOnLastWindowClosed(false)` — app lives in system tray on close.
- Dark theme applied from `resources/style.qss`.
- **Windows build** uses vcpkg (`vcpkg.json`); **Linux** uses system packages.
- AppImage packaging: `packaging/linux/appimage.sh` (requires linuxdeploy + plugin-qt).
- Release workflow: tag `v*` triggers CI that builds and uploads AppImage + Windows zip.
- No README; this file is the primary orientation.
