#include "logger.hpp"

using namespace mylog;

int main()
{
    FileManager::setBasename("it_is_a_test"); // omissible

    Logger::setOutputFunc(FileManager::outputFunc_file);
    Logger::setFlushFunc(FileManager::flushFunc_file);

    // test
    LOG_TRACE << "Hello World";
    LOG_DEBUG << "Hello World";
    LOG_INFO << "Hello World";
    LOG_WARN << "Hello World";
    LOG_ERROR << "Hello World";
    LOG_FATAL << "Hello World";
}