#include "logger.hpp"
#include "fileManager.hpp"

int main()
{
    FileManager::setBasename("hello_"); // omissible

    Logger::setOutputFunction(FileManager::outputFunction_file);
    Logger::setFlushFunction(FileManager::flushFunction_file);

    // test
    LOG_TRACE << "Hello World";
    LOG_DEBUG << "Hello World";
    LOG_INFO << "Hello World";
    LOG_WARN << "Hello World";
    LOG_ERROR << "Hello World";
    LOG_FATAL << "Hello World";
}