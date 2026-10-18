/**
 * @file bench.cpp
 * @brief mylog 性能微基准测试程序
 *
 * 测量各核心热路径每条日志的纳秒级延迟（ns/op）与百万吞吐量（M ops/s）。
 * 覆盖：
 * 1. 运行期抑制过滤路径（亚纳秒级原子判断）；
 * 2. 控制台 stdout -> /dev/null 重定向路径；
 * 3. FileManager 共享单文件落盘路径（开启 64 KiB TLS 写缓冲与原位借出）；
 * 4. FileManager 共享单文件落盘路径（关闭 TLS 缓冲，单锁回退对比）。
 *
 * 用法: bench [总日志条数] [并发线程数]
 */

#include "fileManager.hpp"
#include "logger.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    /**
     * @brief 计算从起始时间点到当前的毫秒数
     *
     * @param t0 起始时间点
     * @return double 经过的毫秒数
     */
    double calculateDurationMs(Clock::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }

    /**
     * @brief 启动并发线程执行批量日志打印并计算平均单条纳秒耗时
     *
     * @param total 总日志条数
     * @param threads 并发线程数
     * @return double 单条日志平均耗时（纳秒）
     */
    double executeBenchmarkRun(size_t total, unsigned threads)
    {
        std::vector<std::thread> pool;
        const size_t per = total / threads;
        auto t0 = Clock::now();
        for (unsigned t = 0; t < threads; ++t)
        {
            pool.emplace_back(
                [per, t]
                {
                    uint64_t sum = 0;
                    for (size_t i = 0; i < per; ++i)
                    {
                        mylog::info("order {} {} {} {} {}", t, i, 12345.6789, 42u, "abcdefgh");
                        sum += i;
                    }
                    // 阻止编译器将循环整体优化消除
                    if (sum == 0xdeadbeefULL)
                        std::fputs("", stderr);
                });
        }
        for (auto &th : pool)
            th.join();
        const double ms = calculateDurationMs(t0);
        return ms * 1e6 / static_cast<double>(per * threads);
    }

    /**
     * @brief 运行多次基准测试并输出最优统计结果
     *
     * @param scenario 测试场景名称
     * @param total 总日志条数
     * @param threads 并发线程数
     */
    void printBenchmarkReport(const char *scenario, size_t total, unsigned threads)
    {
        // 采样 3 轮取最优值，有效抵消操作系统调度抖动与噪声
        double best = 1e18;
        for (int rep = 0; rep < 3; ++rep)
            best = std::min(best, executeBenchmarkRun(total, threads));

        // 输出至 stderr，防止 stdout 重定向到 /dev/null 丢失数据
        std::fprintf(stderr, "%-28s %8.1f ns/op   %10.2f M ops/s  (%u 线程)\n", scenario, best,
                     best > 0 ? 1000.0 / best : 0.0, threads);
        std::fflush(stderr);
    }
} // namespace

int main(int argc, char **argv)
{
    size_t total = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 2000000;
    if (total == 0)
        total = 2000000;
    const unsigned threads = argc > 2 ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10)) : 1;

    std::fprintf(stderr, "=== mylog 性能基准测试 (总条数: %zu, 线程数: %u) ===\n", total, threads);

    // 1. 运行期抑制过滤路径：设定抑制级别为 FATAL，INFO 日志直接被丢弃
    mylog::setSuppressLevel(mylog::LEVEL::FATAL);
    printBenchmarkReport("suppressed (INFO dropped)", total, threads);

    // 2. stdout 路径（重定向至 /dev/null 隔离终端渲染开销）
    mylog::setSuppressLevel(mylog::LEVEL::TRACE);
    std::freopen("/dev/null", "w", stdout);
    printBenchmarkReport("stdout -> /dev/null", total, threads);

    // 3. 文件落盘路径（开启 64 KiB TLS 写缓冲与原位借出零拷贝）
    mylog::Logger::getInstance().setOutputFunction(mylog::FileManager::createFileOutputFunction());
    mylog::Logger::getInstance().setFlushFunction(mylog::FileManager::createFileFlushFunction());
    printBenchmarkReport("file (thread buffer 64KiB)", total, threads);

    // 4. 文件落盘路径（关闭每线程缓冲：退回单锁共享写缓冲对比）
    {
        mylog::FileManager::Config fm = mylog::FileManager::getInstance().getConfig();
        fm.threadBufferCapacity = 0;
        mylog::FileManager::getInstance().setConfig(fm);
    }
    printBenchmarkReport("file (no thread buffer)", total, threads);

    return 0;
}
