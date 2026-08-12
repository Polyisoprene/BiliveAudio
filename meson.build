project('biliveaudio', 'cpp',
  version: '1.3.4',
  default_options: ['cpp_std=c++20', 'buildtype=release', 'warning_level=0'])

qt6 = import('qt6')

# Qt6 modules
qt6_dep = dependency('qt6', modules: ['Core', 'Gui', 'Widgets', 'Network', 'WebSockets'])

# Platform dependencies
spdlog_dep = dependency('spdlog', method: 'pkg-config')

if host_machine.system() == 'linux'
  mpv_dep = dependency('mpv', method: 'pkg-config')
  brotli_dep = dependency('libbrotlidec', method: 'pkg-config')
  qrencode_dep = dependency('libqrencode', method: 'pkg-config')
  jemalloc_dep = declare_dependency(link_args: '-ljemalloc')
else
  brotli_dep = dependency('brotli', required: false)
  qrencode_dep = dependency('libqrencode', required: false)
endif

# Source files
sources = files(
  'src/main.cpp',
  'src/core/BilibiliApi.cpp',
  'src/core/AuthManager.cpp',
  'src/core/LiveMonitor.cpp',
  'src/core/StreamPlayer.cpp',
  'src/core/DanmakuManager.cpp',
  'src/core/AppController.cpp',
  'src/ui/MainWindow.cpp',
  'src/ui/LoginDialog.cpp',
  'src/ui/LiveListWidget.cpp',
  'src/ui/PlayerControl.cpp',
  'src/ui/TrayManager.cpp',
  'src/ui/DanmakuPanel.cpp',
  'src/ui/DanmakuBubble.cpp',
  'src/ui/SettingsDialog.cpp',
  'src/utils/Settings.cpp',
)

# Headers needing MOC (QObject subclasses)
moc_headers = files(
  'src/core/BilibiliApi.h',
  'src/core/AuthManager.h',
  'src/core/LiveMonitor.h',
  'src/core/StreamPlayer.h',
  'src/core/DanmakuManager.h',
  'src/core/AppController.h',
  'src/ui/MainWindow.h',
  'src/ui/LoginDialog.h',
  'src/ui/LiveListWidget.h',
  'src/ui/PlayerControl.h',
  'src/ui/TrayManager.h',
  'src/ui/DanmakuPanel.h',
  'src/ui/DanmakuBubble.h',
  'src/ui/SettingsDialog.h',
)

# Qt preprocessing: MOC + RCC
processed = qt6.preprocess(
  moc_headers: moc_headers,
  qresources: 'resources/resources.qrc',
)

# Link deps
all_deps = [qt6_dep, spdlog_dep]

if host_machine.system() == 'linux'
  all_deps += [mpv_dep, brotli_dep, qrencode_dep, jemalloc_dep]
endif

executable('BiliveAudio',
  sources, processed,
  dependencies: all_deps,
  include_directories: include_directories('src'),
  install: true,
)
