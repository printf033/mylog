#include "logger.hpp"
#include "multiBuffer.hpp"
#include <unistd.h>
#include <thread>

int main(int argc, char *argv[])
{
    std::filesystem::path config = std::filesystem::current_path();
    switch (argc)
    {
    case 2:
        config = argv[1];
        [[fallthrough]];
    default:
        std::cout << "config file path: " << config << std::endl;
    }
    Configer::getInstance().load(config);

    Logger::getInstance().setOutputFunction(MultiBuffer::outputFunction_buf_file_r);
    Logger::getInstance().setFlushFunction(MultiBuffer::flushFunction_buf_file_r);

    std::thread thr001([]
                       {while (true){LOG_INFO("I'm thread 1, sleep 1s");sleep(1);} });
    std::thread thr002([]
                       {while (true){LOG_WARN("I'm thread 2, sleep 2s");sleep(2);} });
    std::thread thr003([]
                       {while (true){LOG_ERROR("I'm thread 3, sleep 3s");sleep(3);} });

    if (thr001.joinable())
        thr001.join();
    if (thr002.joinable())
        thr002.join();
    if (thr003.joinable())
        thr003.join();
}