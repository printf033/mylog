#include <iostream>
#include <string>

#ifndef LOGMESSAGE_HPP
#define LOGMESSAGE_HPP

namespace mylog
{
    enum class LogLevel
    {
        TRACE = 0,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL
    };
    
    class LogMessage
    {
        std::string header_;
        std::string text_;
        LogLevel level_;

        static const char **pLv2Str;

    public:
        LogMessage(const LogLevel &level,
                   const std::string &filename,
                   const std::string &funcname,
                   const int line);
        ~LogMessage() = default;
        LogMessage(const LogMessage &) = delete;
        LogMessage &operator=(const LogMessage &) = delete;
        LogMessage(LogMessage &&) = delete;
        LogMessage &operator=(LogMessage &&) = delete;
        const std::string toString() const;
        const LogLevel getLogLevel() const;
        template <class T>
        LogMessage &operator<<(T &&text)
        {
            text_ += text;
            return *this;
        }
        static void setTerminalVivid();
    };

} // namespace mylog
#endif