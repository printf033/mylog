#include "Logger.hpp"

using namespace mylog;

int main()
{
    LogMessage::_setTerminalColorful();
    
    // test
    LOG_TRACE << "Hello World";
    LOG_DEBUG << "Hello World";
    LOG_INFO << "Hello World";
    LOG_WARN << "Hello World";
    LOG_ERROR << "Hello World";
    LOG_FATAL << "Hello World";
}