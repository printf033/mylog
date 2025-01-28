#include <iostream>
#include <sstream>
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

        static const char **_LvToStr;

    public:
        LogMessage(const LogLevel &level,
                   const std::string &filename,
                   const std::string &funcname,
                   const int line);
        ~LogMessage();
        const std::string toString() const;
        const LogLevel getLogLevel() const;
        template <class T>
        LogMessage &operator<<(const T &text)
        {
            std::stringstream ss;
            ss << text;
            text_ += ss.str();
            return *this;
        }
        static void _setTerminalColorful();
    };

} // namespace mylog
#endif