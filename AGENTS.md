# BiliveAudio — agent guide

CMake + Qt6 + libmpv desktop app for listening to Bilibili live streams (audio-only).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Linux deps (Ubuntu 24.04): `qt6-base-dev qt6-websockets-dev libmpv-dev libspdlog-dev libqrencode-dev libbrotli-dev libjemalloc-dev libgl-dev cmake g++` (add `libgtest-dev` to build tests).

## Tests

Google Test suites under `tests/` (danmaku model, settings, API signing/parsing, player state, memory bounds/leak diagnostics):

```bash
cmake -S . -B build-tests -DBUILD_TESTS=ON
cmake --build build-tests -j$(nproc)
ctest --test-dir build-tests
```

CI (`.github/workflows/release.yml`) does NOT run tests — only builds and packages on tag push.

## Architecture

| Directory | Purpose |
|-----------|---------|
| `src/core/` | AppController (wiring/state machine), BilibiliApi (HTTP + w_rid signing), AuthManager (QR login/cookie), LiveMonitor (poll followed streams), StreamPlayer (libmpv wrapper), DanmakuManager (WebSocket) |
| `src/ui/` | MainWindow, LoginDialog (QR code), LiveListWidget, PlayerControl, DanmakuBubble, DanmakuPanel, SettingsDialog, TrayManager; `DanmakuWindow.cpp/h` exists but is NOT wired into any build target |
| `src/models/` | Plain data structs: UserInfo, LiveRoom, FollowedUser, Danmaku |
| `src/utils/` | Logger (spdlog async, daily files; header-only), Settings (QSettings) |

Entrypoint: `src/main.cpp` (`main()`)

## Important details

- Tests exist (see above); there is **no linter or typecheck** config.
- Primary documentation is `README.md` — keep it in sync when behavior changes.
- Logs go to the platform app-data dir (Linux: `~/.local/share/BiliveAudio/BiliveAudio/logs/app.log`), daily rotation, 7-day retention by default; path/retention configurable in Settings (takes effect after restart).
- Settings persist via QSettings (cookie, volume, window geometry, last room ID).
- `QApplication::setQuitOnLastWindowClosed(false)` — app lives in system tray on close.
- Dark theme applied from `resources/style.qss`.
- **Windows build** uses vcpkg (`vcpkg.json`) + mpv from NuGet (`Endpne.LibMPV.Windows`); **Linux** uses system packages.
- AppImage packaging: `packaging/linux/appimage.sh` (requires linuxdeploy + plugin-qt).
- Release workflow: tag `v*` triggers CI that builds and uploads AppImage + Windows zip.
- StreamPlayer gotchas: mpv events must be processed on the main thread via the wakeup callback → `Q_INVOKABLE processEvents()` (it is invoked by string name through the meta-object system, so keep it Q_INVOKABLE); `demuxer-cache-state` values must be read by their `mpv_node.format` (fw-bytes is INT64).
