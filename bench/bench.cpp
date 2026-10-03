// mylog 微基准：测量热路径每条的纳秒成本。
// 用法: bench [条数] [线程数]
// 只使用公开 API，可与改造前后的版本直接对比。
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

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// 每线程执行 N 次日志；返回单条耗时（纳秒）
double runThreads(size_t total, unsigned threads)
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
                    LOG_INFO("order {} {} {} {} {}", t, i, 12345.6789, 42u, "abcdefgh");
                    sum += i;
                }
                // 阻止整个循环被优化掉
                if (sum == 0xdeadbeefULL)
                    std::fputs("", stderr);
            });
    }
    for (auto &th : pool)
        th.join();
    const double ms = msSince(t0);
    return ms * 1e6 / static_cast<double>(per * threads);
}

void report(const char *what, size_t total, unsigned threads)
{
    // 取 3 轮最好值，抵消调度噪声
    double best = 1e18;
    for (int rep = 0; rep < 3; ++rep)
        best = std::min(best, runThreads(total, threads));
    // 走 stderr：stdout 可能已被重定向到 /dev/null
    std::fprintf(stderr, "%-28s %8.1f ns/op   %10.2f M ops/s  (%u 线程)\n", what, best,
                 best > 0 ? 1000.0 / best : 0.0, threads);
    std::fflush(stderr);
}
} // namespace

int main(int argc, char **argv)
{
    size_t total = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 2000000;
    if (total == 0)
        total = 2000000;
    const unsigned threads =
        argc > 2 ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10)) : 1;

    std::fprintf(stderr, "mylog bench: %zu 条/组, %u 线程\n", total, threads);

    // 1) 编译期/运行时裁剪路径：INFO 被抑制，只付判断成本
    mylog::Logger::getInstance().setSuppressLevel(mylog::LEVEL::FATAL);
    report("suppressed (INFO dropped)", total, threads);

    // 2) stdout 路径（重定向到 /dev/null 以隔离终端成本）
    mylog::Logger::getInstance().setSuppressLevel(mylog::LEVEL::TRACE);
    std::freopen("/dev/null", "w", stdout);
    report("stdout -> /dev/null", total, threads);

    // 3) 文件路径（每线程缓冲 64KiB + 共享写缓冲）
    mylog::Logger::getInstance().setOutputFunction(mylog::FileManager::outputFunction_file());
    mylog::Logger::getInstance().setFlushFunction(mylog::FileManager::flushFunction_file());
    report("file (thread buffer)", total, threads);

    // 4) 文件路径，关闭每线程缓冲：退回到「单锁 + 共享缓冲」的旧行为，用于对比
    {
        mylog::FileManager::Config fm = mylog::FileManager::getInstance().getConfig();
        fm.threadBufferCapacity = 0;
        mylog::FileManager::getInstance().setConfig(fm);
    }
    report("file (no thread buffer)", total, threads);

    // 备注：此处曾评估「异步后台线程 + 有界环」的三种入队策略（下游为 /dev/null、
    // 文件、以及满时阻塞的背压模式）。实测 AsyncSink 相对同步写文件路径在 p50/p999
    // 上均无优势，故整套异步后端已从项目移除；仅同步路径留在 FileManager。

    return 0;
}
