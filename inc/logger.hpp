#include "logMessage.hpp"
#include "fileManager.hpp"
#include "asyncHelper.hpp"
#include <string>
#include <functional>
#include <memory>

#ifndef LOGGER_HPP
#define LOGGER_HPP

namespace mylog
{
    class Logger
    {
    public:
        using OutputFunc = std::function<void(const std::string &msg)>;
        using FlushFunc = std::function<void(void)>;

    private:
        LogMessage impl_;

        static LogLevel logLevel;
        static OutputFunc outputFunc;
        static FlushFunc flushFunc;

    public:
        Logger(const LogLevel &level,
               const std::string &filename,
               const std::string &funcname,
               const int line);
        ~Logger();
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
        Logger(Logger &&) = delete;
        Logger &operator=(Logger &&) = delete;
        static void setOutputFunc(OutputFunc output_func);
        static void setFlushFunc(FlushFunc flush_func);
        static void setLogLevel(const LogLevel &level);
        static const LogLevel getLogLevel();
        LogMessage &stream();
    };

#define LOG_TRACE                                               \
    if (mylog::LogLevel::TRACE >= mylog::Logger::getLogLevel()) \
    mylog::Logger(mylog::LogLevel::TRACE, __FILE__, __func__, __LINE__).stream()

#define LOG_DEBUG                                               \
    if (mylog::LogLevel::DEBUG >= mylog::Logger::getLogLevel()) \
    mylog::Logger(mylog::LogLevel::DEBUG, __FILE__, __func__, __LINE__).stream()

#define LOG_INFO                                               \
    if (mylog::LogLevel::INFO >= mylog::Logger::getLogLevel()) \
    mylog::Logger(mylog::LogLevel::INFO, __FILE__, __func__, __LINE__).stream()

#define LOG_WARN mylog::Logger(mylog::LogLevel::WARN, __FILE__, __func__, __LINE__).stream()

#define LOG_ERROR mylog::Logger(mylog::LogLevel::ERROR, __FILE__, __func__, __LINE__).stream()

#define LOG_FATAL mylog::Logger(mylog::LogLevel::FATAL, __FILE__, __func__, __LINE__).stream()
}

#endif