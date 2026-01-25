#include "logger.hpp"

int main()
{
    Logger::setTerminalVivid();

    LOG_TRACE("mylog {}", "trace");
    LOG_DEBUG("mylog {}", "debug");
    LOG_INFO("mylog {}", "info");
    LOG_WARN("mylog {}", "warn");
    LOG_ERROR("mylog {}", "error");
    LOG_FATAL("mylog {}", "fatal");
}