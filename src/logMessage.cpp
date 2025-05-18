#include "logMessage.hpp"
#include "timestamp.hpp"
#include <libgen.h>
#include <unistd.h>

namespace mylog
{
    static const char *lv2Str[] = {
        "TRACE",
        "DEBUG",
        "INFO ",
        "WARN ",
        "ERROR",
        "FATAL"};
    static const char *lv2Str_clr[] = {
        "\033[7;32mTRACE\033[0m",
        "\033[7;35mDEBUG\033[0m",
        "\033[7;34mINFO \033[0m",
        "\033[7;33mWARN \033[0m",
        "\033[7;31mERROR\033[0m",
        "\033[5;41mFATAL\033[0m"};
    const char **LogMessage::pLv2Str = lv2Str; // 默认log等级样式为无色彩

    LogMessage::LogMessage(const LogLevel &level,
                           const std::string &filename,
                           const std::string &funcname,
                           const int line) : level_(level)
    {
        char buf[256] = {};
        sprintf(buf, "%s %s [%d]%s:%d:%s >> ",
                Timestamp().toFormattedString().c_str(),
                pLv2Str[static_cast<int>(level_)],
                gettid(),
                basename(const_cast<char *>(filename.c_str())),
                line,
                funcname.c_str());
        header_ = buf;
    }
    const std::string LogMessage::toString() const { return header_ + text_ + '\n'; }
    const LogLevel LogMessage::getLogLevel() const { return level_; }
    void LogMessage::setTerminalVivid() { pLv2Str = lv2Str_clr; }
} // namespace mylog