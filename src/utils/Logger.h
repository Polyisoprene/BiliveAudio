#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <memory>
#include <QString>
#include <filesystem>

class Logger {
public:
    static void init(const std::string &logDir = {})
    {
        std::string dir = logDir.empty() ? defaultLogDir() : logDir;
        std::filesystem::create_directories(dir);

        spdlog::init_thread_pool(8192, 1);

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
            dir + "/app.log", 0, 0, false, 7);

        std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
        auto logger = std::make_shared<spdlog::async_logger>(
            "main", sinks.begin(), sinks.end(),
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);

        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger);
    }

    static void flush() { spdlog::default_logger()->flush(); }

private:
    static std::string defaultLogDir()
    {
        auto home = std::getenv("HOME");
        return (std::string(home ? home : ".") + "/.biliveaudio/logs");
    }
};

#define LOG_TRACE(...)    SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...)    SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...)     SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...)     SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...)    SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::err, __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::critical, __VA_ARGS__)
