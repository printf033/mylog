#include "logger.hpp"
#include "fileManager.hpp"
#include "configer_lua.hpp"
#include "sample_config.hpp"

#include <chrono>
#include <iostream>
#include <thread>

// 写文件样例：每个线程写自己的文件。
//
// 与 3_output_to_file 的对比：
//   3 号样例 所有线程共用同一个文件、同一个 fd，追尾的原子性由内核保证；
//   本样例 每个线程一个文件，连 fd 都不共享，线程之间没有任何写状态往来，
//                 业务线程的尾延迟最稳。
// 代价：文件数 = 活跃线程数 × maxFiles，线程数远大于核数、或线程生命周期很短时
//       会产出大量小文件。
int main(int argc, char *argv[])
{
    const std::filesystem::path config = mylog_sample::resolveConfigPath(argc, argv);
    std::cout << "config file path: " << config << std::endl;
    if (!mylog::reloadConfig(config))
        std::cerr << "配置加载失败，沿用当前配置" << std::endl;

    // 只换绑定函数即可切到「每线程一个文件」，契约与 outputFunction_file() 完全相同，
    // 两者可以随时互相切换（切换后旧的线程缓冲会在下次写入或线程退出时排空）。
    mylog::Logger::getInstance().setOutputFunction(mylog::FileManager::outputFunction_filePerThread());
    mylog::Logger::getInstance().setFlushFunction(mylog::FileManager::flushFunction_filePerThread());

    // 文件名形如 _<basename>_<pid>_<tid>.log：比共享布局多一段 _<tid>，
    // 两种布局的文件名互不匹配，保留策略（maxFiles）也不会互相误删。
    LOG_INFO("I'm the main thread, tick {}", 0);

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

    // 注意：flush 只作用于调用线程自己的那个文件。工作线程的残留数据在它们退出时
    // 已由 TlsBuffer 析构排空，这里 flush 的只是主线程的文件。
    LOG_INFO("all worker threads joined");
    mylog::Logger::getInstance().getConfig().flushFunction();
    return 0;
}
