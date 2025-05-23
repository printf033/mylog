#include "logger.hpp"
#include <unistd.h>
#include <thread>

using namespace mylog;

int main()
{
    Logger::setOutputFunc(AsyncHelper::outputFunc_async_file);
    Logger::setFlushFunc(AsyncHelper::flushFunc_async_file);

    // test
    std::thread thr001([]
                       {while (true){LOG_INFO << "I'm thr001, sleep 1s";sleep(1);} });
    std::thread thr002([]
                       {while (true){LOG_WARN << "I'm thr002, sleep 2s";sleep(2);} });
    std::thread thr003([]
                       {while (true){LOG_ERROR << "I'm thr003, sleep 3s";sleep(3);} });

    if (thr001.joinable())
        thr001.join();
    if (thr002.joinable())
        thr002.join();
    if (thr003.joinable())
        thr003.join();
}