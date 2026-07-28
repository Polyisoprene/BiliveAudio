# Google Test based unit tests
cmake_minimum_required(VERSION 3.20)

set(CMAKE_AUTOMOC ON)

find_package(GTest REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(spdlog REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Test)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

enable_testing()

# QPA offscreen for headless test environments
set(CMAKE_CROSSCOMPILING_EMULATOR)
set(TEST_ENV "QT_QPA_PLATFORM=offscreen")

# Common link libraries for all test targets
set(TEST_COMMON_LIBS
    GTest::gtest_main
    GTest::gmock
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Test
    spdlog::spdlog
    PkgConfig::MPV
)

# ── test_danmaku (no MOC needed) ──
add_executable(test_danmaku test_danmaku.cpp)
target_include_directories(test_danmaku PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_danmaku PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME test_danmaku COMMAND test_danmaku)

# ── test_settings (no MOC needed) ──
add_executable(test_settings test_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/Settings.h
    ${CMAKE_SOURCE_DIR}/src/utils/Settings.cpp
)
target_include_directories(test_settings PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_settings PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME test_settings COMMAND test_settings)

# ── test_bilibiliapi (needs MOC) ──
add_executable(test_bilibiliapi test_bilibiliapi.cpp
    ${CMAKE_SOURCE_DIR}/src/core/BilibiliApi.h
    ${CMAKE_SOURCE_DIR}/src/core/BilibiliApi.cpp
)
target_include_directories(test_bilibiliapi PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_bilibiliapi PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME test_bilibiliapi COMMAND test_bilibiliapi)
set_tests_properties(test_bilibiliapi PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── test_streamplayer (needs MOC) ──
add_executable(test_streamplayer test_streamplayer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/StreamPlayer.h
    ${CMAKE_SOURCE_DIR}/src/core/StreamPlayer.cpp
)
target_include_directories(test_streamplayer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_streamplayer PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME test_streamplayer COMMAND test_streamplayer)
set_tests_properties(test_streamplayer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
