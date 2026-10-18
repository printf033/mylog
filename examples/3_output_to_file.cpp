#include "configer_lua.hpp"
#include "example_config.hpp"
#include "fileManager.hpp"
#include "logger.hpp"

#include <chrono>
#include <iostream>
#include <thread>

/**
 * @file 3_output_to_file.cpp
 * @brief 共享单文件落盘示例（多线程高并发共享同一个日志文件）
 *
 * 核心原理：
 * 1. 各业务线程独立持有线程局部缓冲（TLS Buffer，默认 64 KiB），写入直接原位格式化至 TLS
 * 缓冲，零互斥锁争用、零二次内存拷贝；
 * 2. 缓冲攒满或定时器到期后，通过 Linux 内核针对 O_APPEND 文件的原子追加写入（write(2)）特性直接落盘；
 * 3. 线程退出时由 TLS 对象析构函数自动排空剩余缓冲；
 * 4. 自动按容量滚动文件，并增量维护保留最新的 maxFiles 个历史日志文件。
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

    // 2. 将日志输出重定向至 FileManager 共享单文件落盘后端
    // 采用借出原位缓冲协议（acquire / release），实现 std::format 零拷贝直接构建落盘内容
    mylog::Logger::getInstance().setOutputFunction(mylog::FileManager::createFileOutputFunction());
    mylog::Logger::getInstance().setFlushFunction(mylog::FileManager::createFileFlushFunction());

    // 主线程写入日志
    mylog::info("主线程已成功切换至共享单文件落盘模式");

    // 3. 启动多个并发工作线程，共同写入同一个日志文件（热路径完全无锁）
    const auto worker = [](int id, int ticks, std::chrono::milliseconds gap)
    {
        for (int i = 0; i < ticks; ++i)
        {
            mylog::info("工作线程 {} 正在处理任务: 进度 {}/{}", id, i + 1, ticks);
            std::this_thread::sleep_for(gap);
        }
    };

    std::thread thr1(worker, 1, 5, std::chrono::milliseconds(10));
    std::thread thr2(worker, 2, 5, std::chrono::milliseconds(20));
    std::thread thr3(worker, 3, 5, std::chrono::milliseconds(30));

    thr1.join();
    thr2.join();
    thr3.join();

    mylog::info("所有工作线程已执行完毕并安全汇合");

    // 演示 FATAL 日志输出（系统默认配置为 FATAL_ACTION::NONE，不会终止进程）
    mylog::fatal("这是一条 FATAL 级别通知（演示完成）");

    // 手动排空当前线程剩余缓冲区至磁盘
    mylog::Logger::getInstance().getConfig().flushFunction();
    std::cout << "[example] 共享单文件日志输出演示成功完成" << std::endl;

    return 0;
}
