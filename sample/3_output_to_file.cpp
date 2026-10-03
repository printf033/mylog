#include "logger.hpp"
#include "fileManager.hpp"
#include "configer_lua.hpp"
#include "sample_config.hpp"

#include <chrono>
#include <iostream>
#include <thread>

// 写文件样例：多线程共用同一个文件。
//
// 写文件只有这一种用法 —— FileManager 的写路径本身就是线程安全的，
// 调用方不需要（也不应该）再套一层锁，更不需要区分「单线程版 / 线程安全版」。
int main(int argc, char *argv[])
{
    const std::filesystem::path config = mylog_sample::resolveConfigPath(argc, argv);
    std::cout << "config file path: " << config << std::endl;
    if (!mylog::reloadConfig(config))
        std::cerr << "配置加载失败，沿用当前配置" << std::endl;

    // 把日志交给文件后端。两个函数都线程安全，任何线程调用都可以。
    //
    // threadBufferCapacity > 0（默认 64 KiB）时，每条日志只做一次线程内的 append：
    // 无锁、无系统调用。缓冲攒满、或线程退出时才把整块 write(2) 到同一个
    // O_APPEND fd —— 追尾的原子性由内核的 file description 保证，所以用户态热路径上
    // 不存在锁。线程退出时残留数据由 TlsBuffer 的析构自动排空。
    mylog::Logger::getInstance().setOutputFunction(mylog::FileManager::outputFunction_file());
    mylog::Logger::getInstance().setFlushFunction(mylog::FileManager::flushFunction_file());

    // 主线程直接写，无需任何额外同步
    LOG_TRACE("mylog {}", "trace");
    LOG_DEBUG("mylog {}", "debug");
    LOG_INFO("mylog {}", "info");
    LOG_WARN("mylog {}", "warn");
    LOG_ERROR("mylog {}", "error");

    // 多个业务线程并发写同一个文件
    const auto worker = [](int id, int ticks, std::chrono::milliseconds gap)
    {
        for (int i = 0; i < ticks; ++i)
        {
            LOG_INFO("I'm thread {}, tick {}", id, i);
            std::this_thread::sleep_for(gap);
        }
    };

    std::thread thr1(worker, 1, 5, std::chrono::milliseconds(10));
    std::thread thr2(worker, 2, 5, std::chrono::milliseconds(20));
    std::thread thr3(worker, 3, 5, std::chrono::milliseconds(30));

    thr1.join();
    thr2.join();
    thr3.join();

    LOG_INFO("all worker threads joined");

    // LOG_FATAL 放在最后：fatalAction 配成 abort 时进程会立刻退出，
    // 放在这里就不会打断上面的多线程演示。
    LOG_FATAL("mylog {}", "fatal");

    mylog::Logger::getInstance().getConfig().flushFunction();
    return 0;
}
