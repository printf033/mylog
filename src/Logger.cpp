#include "Logger.hpp"
#include <string.h>
#include <stdio.h>

namespace mylog
{
    // 默认log等级为INFO
    LogLevel initLogLevel()
    {
        if (getenv("LOG_TRACE"))
            return LogLevel::TRACE;
        else if (getenv("LOG_DEBUG"))
            return LogLevel::DEBUG;
        else
            return LogLevel::INFO;
    }
    LogLevel Logger::_level = initLogLevel();
    // 默认log输出为stdout
    void outputFunc_stdout(const std::string &msg) { printf("%s", msg.c_str()); }
    void flushFunc_stdout() { fflush(stdout); }
    Logger::OutputFunc Logger::_output_func = outputFunc_stdout;
    Logger::FlushFunc Logger::_flush_func = flushFunc_stdout;

    Logger::Logger(const LogLevel &level,
                   const std::string &filename,
                   const std::string &funcname,
                   const int line)
        : impl_(level, filename, funcname, line) {}
    Logger::~Logger()
    {
        _output_func(impl_.toString());
        if (impl_.getLogLevel() == LogLevel::FATAL)
        {
            _flush_func();
            fprintf(stderr, "****** FATAL Error Occured! Log Killed the Process! ******\n");
            exit(1);
        }
    }
    LogMessage &Logger::stream() { return impl_; }
    const LogLevel Logger::_getLogLevel() { return _level; }
    void Logger::_setLogLevel(const LogLevel &level) { _level = level; }
    void Logger::_setOutputFunc(OutputFunc output_func) { _output_func = output_func; }
    void Logger::_setFlushFunc(FlushFunc flush_func) { _flush_func = flush_func; }
}
