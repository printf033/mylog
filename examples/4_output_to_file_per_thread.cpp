#include "example_config.hpp"
#include "fileManager.hpp"
#include "logger.hpp"
#include "lua_config.hpp"
#include <chrono>
#include <iostream>
#include <thread>

/**
 * @file 4_output_to_file_per_thread.cpp
 * @brief 每线程独占文件落盘示例（各线程维护专属独立的日志文件）
 *
 * 核心原理与适用场景：
 * 1. 每个线程独占一个文件描述符（fd）与私有 TLS 缓冲，线程间完全零同步、零互斥锁、零写入争用；
 * 2. 彻底杜绝多线程写同一个共享文件时的内核锁竞争与换文件滚动时的排空等待，提供极致平稳的长尾延迟（p99/p999）；
 * 3. 产出的文件名格式为：年月日_时分秒_纳秒_线程号.log，带有专属的操作系统线程 ID；
 * 4. 适合长生命周期的固定计算/网络工作线程池场景。
 */

int main(int argc, char *argv[])
{
    // 1. 探测并加载配置文件
    const std::filesystem::path config = mylog_example::resolveConfigPath(argc, argv);
    std::cout << "[example] 加载配置文件: " << config << std::endl;
    if (!mylog::reloadConfig(config))
    {
        std::cerr << "[example] 配置加载失败，沿用当前默认配置" << std::endl;
    }

    // 2. 将日志输出重定向至每线程独立文件落盘后端
    mylog::Logger::getInstance().setOutputFunction(mylog::FileManager::createPerThreadFileOutputFunction());
    mylog::Logger::getInstance().setFlushFunction(mylog::FileManager::createPerThreadFileFlushFunction());

    // 主线程写入自身专属的日志文件
    mylog::info("主线程已切换至每线程独占文件模式: tid={}", mylog::detail::getThreadId());

    // 3. 启动多个并发工作线程，各自向自身独占的文件写日志
    const auto worker = [](int id, int ticks, std::chrono::milliseconds gap)
    {
        for (int i = 0; i < ticks; ++i)
        {
            mylog::info("独占文件线程 {} (系统 tid={}) 处理步骤 {}/{}", id, mylog::detail::getThreadId(), i + 1, ticks);
            std::this_thread::sleep_for(gap);
        }
    };

    std::thread thr1(worker, 1, 5, std::chrono::milliseconds(10));
    std::thread thr2(worker, 2, 5, std::chrono::milliseconds(20));
    std::thread thr3(worker, 3, 5, std::chrono::milliseconds(30));

    thr1.join();
    thr2.join();
    thr3.join();

    mylog::info("所有独占文件工作线程已汇合完毕");

    // 刷新主线程专属日志文件
    mylog::Logger::getInstance().getConfig().flushFunction();
    std::cout << "[example] 每线程独占文件输出演示成功完成" << std::endl;

    return 0;
}
