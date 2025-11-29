#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <sstream>
#include <functional>
#include <cstdio>
#include "timestamp.hpp"
#include <unistd.h>
#include <libgen.h>

class Logger
{
public:
    enum class Level
    {
        TRACE = 0,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL
    };

private:
    static inline Level triggerLevel_global = Level::TRACE;

    static inline const char *LV2STR[] = {
        "TRACE",
        "DEBUG",
        "INFO ",
        "WARN ",
        "ERROR",
        "FATAL"};
    static inline const char *LV2STR_vivid[] = {
        "\033[7;32mTRACE\033[0m",
        "\033[7;35mDEBUG\033[0m",
        "\033[7;34mINFO \033[0m",
        "\033[7;33mWARN \033[0m",
        "\033[7;31mERROR\033[0m",
        "\033[5;41mFATAL\033[0m"};
    static inline const char **lv2Str_global = LV2STR;

    using OutputFunction = std::function<void(const std::string &msg)>;
    using FlushFunction = std::function<void(void)>;
    static void output_stdout(const std::string &msg) { printf("%s", msg.c_str()); }
    static void flush_stdout() { fflush(stdout); }
    static inline OutputFunction outputFunction_global = output_stdout;
    static inline FlushFunction flushFunction_global = flush_stdout;

    Level level_;
    std::string header_;
    std::string text_;

public:
    Logger(const Level &level,
           const std::string &filename,
           const std::string &funcname,
           const int line) : level_(level)
    {
        char buf[512] = {};
        sprintf(buf, "%s %s [%d]%s:%d:%s > ",
                Timestamp().toFormattedString(true).c_str(),
                lv2Str_global[static_cast<int>(level_)],
                gettid(),
                basename(const_cast<char *>(filename.c_str())),
                line,
                funcname.c_str());
        header_ = buf;
    }
    ~Logger()
    {
        outputFunction_global(header_ + text_ + '\n');
        if (level_ == Level::FATAL)
        {
            flushFunction_global();
            fprintf(stderr, "****** FATAL Error Occured! Logger Killed the Process! ******\n");
            exit(EXIT_FAILURE);
        }
    }
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;
    static Level getTriggerLevel() { return triggerLevel_global; }
    Logger &operator<<(std::string_view sv)
    {
        text_ += sv;
        return *this;
    }
    Logger &operator<<(const char *s)
    {
        text_ += s;
        return *this;
    }
    Logger &operator<<(char c)
    {
        text_ += c;
        return *this;
    }
    template <class T>
    Logger &operator<<(const T &value)
    {
        if constexpr (std::is_arithmetic_v<T>)
        {
            text_ += std::to_string(value);
        }
        else
        {
            std::ostringstream oss;
            oss << value;
            text_ += oss.str();
        }
        return *this;
    }

    static void setTriggerLevel(const Level &level) { triggerLevel_global = level; }
    static void setTerminalVivid() { lv2Str_global = LV2STR_vivid; }
    static void setOutputFunction(const OutputFunction &outputFunction) { outputFunction_global = outputFunction; }
    static void setFlushFunction(const FlushFunction &flushFunction) { flushFunction_global = flushFunction; }
};

#define LOG_TRACE                                          \
    if (Logger::Level::TRACE >= Logger::getTriggerLevel()) \
    Logger(Logger::Level::TRACE, __FILE__, __func__, __LINE__)

#define LOG_DEBUG                                          \
    if (Logger::Level::DEBUG >= Logger::getTriggerLevel()) \
    Logger(Logger::Level::DEBUG, __FILE__, __func__, __LINE__)

#define LOG_INFO                                          \
    if (Logger::Level::INFO >= Logger::getTriggerLevel()) \
    Logger(Logger::Level::INFO, __FILE__, __func__, __LINE__)

#define LOG_WARN                                          \
    if (Logger::Level::WARN >= Logger::getTriggerLevel()) \
    Logger(Logger::Level::WARN, __FILE__, __func__, __LINE__)

#define LOG_ERROR Logger(Logger::Level::ERROR, __FILE__, __func__, __LINE__)

#define LOG_FATAL Logger(Logger::Level::FATAL, __FILE__, __func__, __LINE__)

#endif