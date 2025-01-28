#include "Logger.hpp"

using namespace mylog;

int main()
{
    FileManager::_setBasename("test");

    Logger::_setOutputFunc(FileManager::_outputFunc_file);
    Logger::_setFlushFunc(FileManager::_flushFunc_file);

    // test
    LOG_TRACE << "Hello World";
    LOG_DEBUG << "Hello World";
    LOG_INFO << "Hello World";
    LOG_WARN << "Hello World";
    LOG_ERROR << "Hello World";
    LOG_FATAL << "Hello World";
}