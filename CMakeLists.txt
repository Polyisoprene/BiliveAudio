cmake_minimum_required(VERSION 3.20)
project(BiliveAudio VERSION 1.3.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets)
find_package(spdlog REQUIRED)

if(WIN32)
    find_package(unofficial-brotli CONFIG REQUIRED)
    find_path(QRENCODE_INCLUDE_DIR NAMES qrencode.h REQUIRED)
    find_library(QRENCODE_LIBRARY_RELEASE qrencode REQUIRED)
    find_library(QRENCODE_LIBRARY_DEBUG qrencoded)
    find_path(MPV_INCLUDE_DIR NAMES mpv/client.h REQUIRED)
    find_library(MPV_LIBRARY_RELEASE mpv REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(BROTLIDEC REQUIRED IMPORTED_TARGET libbrotlidec)
    pkg_check_modules(QRENCODE REQUIRED IMPORTED_TARGET libqrencode)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
endif()

set(SOURCES
    src/main.cpp
    src/core/BilibiliApi.cpp
    src/core/AuthManager.cpp
    src/core/LiveMonitor.cpp
    src/core/StreamPlayer.cpp
    src/core/DanmakuManager.cpp
    src/core/AppController.cpp
    src/ui/MainWindow.cpp
    src/ui/LoginDialog.cpp
    src/ui/LiveListWidget.cpp
    src/ui/PlayerControl.cpp
    src/ui/TrayManager.cpp
    src/ui/DanmakuBubble.cpp
    src/ui/DanmakuPanel.cpp
    src/ui/SettingsDialog.cpp
    src/utils/Settings.cpp
)

set(HEADERS
    src/core/BilibiliApi.h
    src/core/AuthManager.h
    src/core/LiveMonitor.h
    src/core/StreamPlayer.h
    src/core/DanmakuManager.h
    src/core/AppController.h
    src/ui/MainWindow.h
    src/ui/LoginDialog.h
    src/ui/LiveListWidget.h
    src/ui/PlayerControl.h
    src/ui/TrayManager.h
    src/ui/DanmakuBubble.h
    src/ui/DanmakuPanel.h
    src/ui/SettingsDialog.h
    src/utils/Logger.h
    src/utils/Settings.h
    src/models/UserInfo.h
    src/models/LiveRoom.h
    src/models/FollowedUser.h
    src/models/Danmaku.h
)

option(BUILD_TESTS "Build unit tests" OFF)

set(RESOURCES resources/resources.qrc)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
    target_include_directories(${PROJECT_NAME} PRIVATE ${QRENCODE_INCLUDE_DIR} ${MPV_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::WebSockets
        spdlog::spdlog
        unofficial::brotli::brotlidec
        ${MPV_LIBRARY_RELEASE}
        optimized ${QRENCODE_LIBRARY_RELEASE}
        debug ${QRENCODE_LIBRARY_DEBUG}
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    find_library(JEMALLOC_LIB jemalloc)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::WebSockets
        spdlog::spdlog
        PkgConfig::MPV
        PkgConfig::BROTLIDEC
        PkgConfig::QRENCODE
        ${JEMALLOC_LIB}
    )
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
