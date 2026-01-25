#include "logger.hpp"
#include "fileManager.hpp"

int main()
{
    FileManager::setBasename("file"); // omissible

    Logger::setOutputFunction(FileManager::outputFunction_file);
    Logger::setFlushFunction(FileManager::flushFunction_file);

    LOG_TRACE("mylog {}", "trace");
    LOG_DEBUG("mylog {}", "debug");
    LOG_INFO("mylog {}", "info");
    LOG_WARN("mylog {}", "warn");
    LOG_ERROR("mylog {}", "error");
    LOG_FATAL("mylog {}", "fatal");
}