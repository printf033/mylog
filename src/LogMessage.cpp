#include "LogMessage.hpp"
#include "Timestamp.hpp"
#include <libgen.h>
#include <unistd.h>

namespace mylog
{
    // 缓冲区大小（单位：Byte）
    const int LOGMESSAGE_BUFF_SIZE = 256;
    // 默认log等级样式为非终端彩色
    static const char *LvToStr[] = {
        "TRACE",
        "DEBUG",
        "INFO ",
        "WARN ",
        "ERROR",
        "FATAL"};
    static const char *LvToStr_clr[] = {
        "\033[7;32mTRACE\033[0m",
        "\033[7;35mDEBUG\033[0m",
        "\033[7;34mINFO \033[0m",
        "\033[7;33mWARN \033[0m",
        "\033[7;31mERROR\033[0m",
        "\033[5;41mFATAL\033[0m"};
    const char **LogMessage::_LvToStr = LvToStr;

    LogMessage::LogMessage(const LogLevel &level,
                           const std::string &filename,
                           const std::string &funcname,
                           const int line)
        : header_{}, text_{}, level_(level)
    {
        char buf[LOGMESSAGE_BUFF_SIZE] = {};
        sprintf(buf, "%s %s [%d]%s:%d:%s >> ",
                Timestamp::_Now().toFormattedString().c_str(),
                _LvToStr[static_cast<int>(level_)],
                gettid(),
                basename(const_cast<char *>(filename.c_str())),
                line,
                funcname.c_str());
        header_ = buf;
    }
    LogMessage::~LogMessage() {}
    const std::string LogMessage::toString() const { return header_ + text_ + "\n"; }
    const LogLevel LogMessage::getLogLevel() const { return level_; }
    void LogMessage::_setTerminalColorful() { _LvToStr = LvToStr_clr; }
} // namespace mylog