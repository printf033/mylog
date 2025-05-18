#include "logger.hpp"
#include <string.h>
#include <stdio.h>

namespace mylog
{
    LogLevel initLogLevel()
    {
        if (getenv("LOG_INFO"))
            return LogLevel::INFO;
        else if (getenv("LOG_DEBUG"))
            return LogLevel::DEBUG;
        else
            return LogLevel::TRACE;
    }
    LogLevel Logger::logLevel = initLogLevel(); // 默认log等级为TRACE
    void outputFunc_stdout(const std::string &msg) { printf("%s", msg.c_str()); }
    void flushFunc_stdout() { fflush(stdout); }
    Logger::OutputFunc Logger::outputFunc = outputFunc_stdout; // 默认log输出为stdout
    Logger::FlushFunc Logger::flushFunc = flushFunc_stdout;    // 默认log刷新为stdout

    Logger::Logger(const LogLevel &level,
                   const std::string &filename,
                   const std::string &funcname,
                   const int line) : impl_(level, filename, funcname, line) {}
    Logger::~Logger()
    {
        outputFunc(impl_.toString());
        if (impl_.getLogLevel() == LogLevel::FATAL)
        {
            flushFunc();
            fprintf(stderr, "****** FATAL Error Occured! Log Killed the Process! ******\n");
            exit(EXIT_FAILURE);
        }
    }
    LogMessage &Logger::stream() { return impl_; }
    const LogLevel Logger::getLogLevel() { return logLevel; }
    void Logger::setLogLevel(const LogLevel &level) { logLevel = level; }
    void Logger::setOutputFunc(OutputFunc output_func) { outputFunc = output_func; }
    void Logger::setFlushFunc(FlushFunc flush_func) { flushFunc = flush_func; }
}
