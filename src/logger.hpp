#pragma once

#include "myconfiger.hpp"
#include "timestamp.hpp"
#include <unistd.h>
#include <format>
#include <type_traits>

class Logger
{
    OptionsLogger options_;

    Logger() noexcept = default;
    ~Logger() noexcept = default;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

public:
    inline static Logger &getInstance()
    {
        static Logger instance;
        return instance;
    }
    template <typename... Args>
    inline void log(LEVEL level, const char *file, int line, const char *func,
                    std::format_string<Args...> fmt, Args &&...args)
    {
        thread_local std::string msg;
        msg.clear();
        if (msg.capacity() < 256)
            msg.reserve(256);
        std::format_to(std::back_inserter(msg), "{} {} [{}]{}:{}:{} >",
                       Timestamp().toFormattedString(),
                       options_.lv2str[static_cast<int>(level)],
                       gettid(), file, line, func);
        std::vformat_to(std::back_inserter(msg), fmt.get(),
                        std::make_format_args(args...));
        msg.push_back('\n');
        options_.outputFunction(msg);
        if (level == LEVEL::FATAL)
        {
            options_.flushFunction();
            exit(EXIT_FAILURE);
        }
    }
    inline void log(LEVEL level, const char *file, int line, const char *func,
                    std::string_view fmt)
    {
        thread_local std::string msg;
        msg.clear();
        if (msg.capacity() < 256)
            msg.reserve(256);
        std::format_to(std::back_inserter(msg), "{} {} [{}]{}:{}:{} >{}\n",
                       Timestamp().toFormattedString(),
                       options_.lv2str[static_cast<int>(level)],
                       gettid(), file, line, func, fmt);
        options_.outputFunction(msg);
        if (level == LEVEL::FATAL)
        {
            options_.flushFunction();
            exit(EXIT_FAILURE);
        }
    }
    inline LEVEL getSuppressLevel() const noexcept
    {
        return options_.suppressLevel;
    }
    inline static void setSuppressLevel(LEVEL level) noexcept
    {
        getInstance().options_.suppressLevel = level;
    }
    inline static void setTerminalVivid() noexcept
    {
        getInstance().options_.lv2str = OptionsLogger::LV2STR_vivid.data();
    }
    inline static void setOutputFunction(const OutputFunction &outputFunc) noexcept
    {
        getInstance().options_.outputFunction = outputFunc;
    }
    inline static void setFlushFunction(const FlushFunction &flushFunc) noexcept
    {
        getInstance().options_.flushFunction = flushFunc;
    }
};

#define LOG_TRACE(...)                                           \
    if (LEVEL::TRACE < Logger::getInstance().getSuppressLevel()) \
        ;                                                        \
    else                                                         \
        Logger::getInstance().log(LEVEL::TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_DEBUG(...)                                           \
    if (LEVEL::DEBUG < Logger::getInstance().getSuppressLevel()) \
        ;                                                        \
    else                                                         \
        Logger::getInstance().log(LEVEL::DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_INFO(...)                                           \
    if (LEVEL::INFO < Logger::getInstance().getSuppressLevel()) \
        ;                                                       \
    else                                                        \
        Logger::getInstance().log(LEVEL::INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_WARN(...)                                           \
    if (LEVEL::WARN < Logger::getInstance().getSuppressLevel()) \
        ;                                                       \
    else                                                        \
        Logger::getInstance().log(LEVEL::WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_ERROR(...) Logger::getInstance().log(LEVEL::ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_FATAL(...) Logger::getInstance().log(LEVEL::FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)