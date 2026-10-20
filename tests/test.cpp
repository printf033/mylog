/**
 * @file test.cpp
 * @brief mylog 全量功能、高并发完整性与安全加固验证测试套件
 *
 * 覆盖范围：
 * 1. 控制字符 / ANSI 转义（日志注入防护）与开关
 * 2. 运行时抑制级别（枚举常量与字符串反射双重校验）
 * 3. 单条消息最大长度截断与配置边界安全钳制
 * 4. 多线程高并发写入完整性（开启 / 关闭 TLS 缓冲）
 * 5. TLS 线程缓冲排空机制（线程退出与显式 flush）
 * 6. 单文件按容量滚动与 maxFiles 历史文件保留策略
 * 7. 并发写入下的原子滚动与行边界完整性校验
 * 8. 每线程独占文件模式（写入隔离、独立容量滚动与保留）
 * 9. 线程防重入防护（避免回调递归死锁）
 * 10. FATAL 动作（NONE 默认不退出 / ABORT 触发 SIGABRT）
 * 11. Basename 格式校验与沙箱 Lua 配置加载（含命令注入防御断言）
 * 12. C++26 静态反射（枚举反查、结构体自动展开、[[= mylog::sensitive]] 自动脱敏）
 * 13. 现代 C++26 无宏 API（基于 std::source_location 源码位置注入）
 */

#include "configer_lua.hpp"
#include "fileManager.hpp"
#include "logger.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++g_checks;                                                                                                    \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++g_failures;                                                                                              \
            std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                     \
        }                                                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        ++g_checks;                                                                                                    \
        if (!((a) == (b)))                                                                                             \
        {                                                                                                              \
            ++g_failures;                                                                                              \
            std::fprintf(stderr, "  FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b);                              \
        }                                                                                                              \
    } while (0)

    /**
     * @brief 打印当前正在运行的测试章节名称
     *
     * @param name 测试章节名称
     */
    void section(const char *name)
    {
        std::fprintf(stderr, "\n[%s]\n", name);
    }

    std::mutex g_captureMtx;
    std::string g_capture;

    /**
     * @brief 测试内存捕获输出回调
     *
     * @param msg 待输出的日志内容
     */
    void captureOutput(void * /*ctx*/, std::string_view msg)
    {
        std::lock_guard<std::mutex> lock(g_captureMtx);
        g_capture.append(msg.data(), msg.size());
    }

    /**
     * @brief 空刷盘回调
     */
    void noFlush(void * /*ctx*/) {}

    /**
     * @brief 获取内存捕获器中累积的日志字符串副本
     *
     * @return std::string 捕获的日志内容
     */
    std::string captured()
    {
        std::lock_guard<std::mutex> lock(g_captureMtx);
        return g_capture;
    }

    /**
     * @brief 清空内存捕获器中的内容
     */
    void clearCaptured()
    {
        std::lock_guard<std::mutex> lock(g_captureMtx);
        g_capture.clear();
    }

    /**
     * @brief 配置全局 Logger 走内存捕获回调
     */
    void useCaptureOutput()
    {
        mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
        cfg.outputFunction = mylog::OutputFunction{&captureOutput, nullptr};
        cfg.flushFunction = mylog::FlushFunction{&noFlush, nullptr};
        cfg.fatalAction = mylog::FATAL_ACTION::NONE;
        mylog::Logger::getInstance().setConfig(cfg);
        mylog::setSuppressLevel(mylog::LEVEL::TRACE);
        clearCaptured();
    }

    /**
     * @brief 配置全局 Logger 走 FileManager 共享单文件落盘后端
     */
    void useFileOutput()
    {
        mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
        cfg.outputFunction = mylog::FileManager::createFileOutputFunction();
        cfg.flushFunction = mylog::FileManager::createFileFlushFunction();
        cfg.fatalAction = mylog::FATAL_ACTION::NONE;
        mylog::Logger::getInstance().setConfig(cfg);
    }

    /**
     * @brief 配置全局 Logger 走 FileManager 每线程独占文件落盘后端
     */
    void useFileOutputPerThread()
    {
        mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
        cfg.outputFunction = mylog::FileManager::createPerThreadFileOutputFunction();
        cfg.flushFunction = mylog::FileManager::createPerThreadFileFlushFunction();
        cfg.fatalAction = mylog::FATAL_ACTION::NONE;
        mylog::Logger::getInstance().setConfig(cfg);
    }

    /**
     * @brief 设置 FileManager 配置参数
     *
     * @param basename 日志文件名前缀
     * @param threadBuffer 线程局部缓冲大小
     * @param fileCapacity 单文件滚动容量
     * @param maxFiles 最多保留的历史文件数
     */
    void setFileConfig(std::string basename, size_t threadBuffer, size_t fileCapacity, size_t maxFiles)
    {
        mylog::FileManager::Config fcfg = mylog::FileManager::getInstance().getConfig();
        fcfg.basename = std::move(basename);
        fcfg.threadBufferCapacity = threadBuffer;
        fcfg.fileCapacity = fileCapacity;
        fcfg.maxFiles = maxFiles;
        mylog::FileManager::getInstance().setConfig(fcfg);
    }

    /**
     * @brief 计算共享单文件模式下的文件名后缀
     *
     * @param basename 文件基名
     * @return std::string 后缀字符串
     */
    std::string pidSuffix(const std::string &basename)
    {
        return "_" + basename + "_" + std::to_string(static_cast<long>(::getpid())) + ".log";
    }

    /**
     * @brief 收集匹配指定基名的所有共享单文件日志路径
     *
     * @param basename 文件基名
     * @return std::vector<std::filesystem::path> 排序后的文件路径列表
     */
    std::vector<std::filesystem::path> collectLogs(const std::string &basename)
    {
        std::vector<std::filesystem::path> out;
        const std::string suffix = pidSuffix(basename);
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(".", ec))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string name = entry.path().filename().string();
            if (name.size() > suffix.size() && name.ends_with(suffix))
                out.push_back(entry.path());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    /**
     * @brief 计算每线程独占模式下的中缀特征字符串
     *
     * @param basename 文件基名
     * @return std::string 中缀字符串
     */
    std::string perThreadInfix(const std::string &basename)
    {
        return "_" + basename + "_" + std::to_string(static_cast<long>(::getpid())) + "_";
    }

    /**
     * @brief 收集匹配指定基名的所有每线程独占模式日志路径
     *
     * @param basename 文件基名
     * @return std::vector<std::filesystem::path> 排序后的文件路径列表
     */
    std::vector<std::filesystem::path> collectPerThreadLogs(const std::string &basename)
    {
        std::vector<std::filesystem::path> out;
        const std::string infix = perThreadInfix(basename);
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(".", ec))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string name = entry.path().filename().string();
            if (name.size() > infix.size() + 4 && name.find(infix) != std::string::npos && name.ends_with(".log"))
                out.push_back(entry.path());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    /**
     * @brief 读取整个文本文件的全部内容
     *
     * @param path 文件路径
     * @return std::string 文件文本内容
     */
    std::string readText(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    /**
     * @brief 从一系列日志文件中读取并按行解析
     *
     * @param files 文件路径列表
     * @return std::vector<std::string> 解析出的所有日志行
     */
    std::vector<std::string> readLinesFrom(const std::vector<std::filesystem::path> &files)
    {
        std::vector<std::string> lines;
        for (const auto &path : files)
        {
            const std::string text = readText(path);
            size_t start = 0;
            while (start < text.size())
            {
                const size_t end = text.find('\n', start);
                if (end == std::string::npos)
                {
                    lines.push_back(text.substr(start));
                    break;
                }
                lines.push_back(text.substr(start, end - start + 1));
                start = end + 1;
            }
        }
        return lines;
    }

    /**
     * @brief 从所有指定基名的共享单文件中回读所有日志行
     *
     * @param basename 文件基名
     * @return std::vector<std::string> 所有日志行
     */
    std::vector<std::string> readAllLines(const std::string &basename)
    {
        return readLinesFrom(collectLogs(basename));
    }

    /**
     * @brief 校验单行日志格式是否完整规范（时间戳、空格、换行符）
     *
     * @param line 待检测的日志行
     * @return true 格式完好无撕裂
     * @return false 格式异常或被截断
     */
    bool lineWellFormed(const std::string &line)
    {
        if (line.size() < 25 || line.back() != '\n')
            return false;
        static constexpr char kLayout[19] = {'d', 'd', 'd', 'd', '-', 'd', 'd', '-', 'd', 'd',
                                             ' ', 'd', 'd', ':', 'd', 'd', ':', 'd', 'd'};
        for (int i = 0; i < 19; ++i)
        {
            const char c = line[static_cast<size_t>(i)];
            if (kLayout[i] == 'd')
            {
                if (c < '0' || c > '9')
                    return false;
            }
            else if (c != kLayout[i])
            {
                return false;
            }
        }
        return line[19] == '.' && line[23] == ' ' && line[20] >= '0' && line[20] <= '9' && line[21] >= '0' &&
               line[21] <= '9' && line[22] >= '0' && line[22] <= '9';
    }

    /**
     * @brief 从格式化日志行中提取纯消息文本（剥离时间、级别、TID、函数名及行尾位置信息）
     *
     * @param line 完整日志行
     * @return std::string 提取出的纯正文内容
     */
    std::string messageOf(const std::string &line)
    {
        const size_t pos = line.find(">>");
        if (pos == std::string::npos)
            return {};
        std::string msg = line.substr(pos + 2);
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
            msg.pop_back();

        const size_t meta = msg.rfind(" (");
        if (meta != std::string::npos && msg.size() > meta + 3 && msg.back() == ')')
        {
            const std::string inner = msg.substr(meta + 2, msg.size() - meta - 3);
            const size_t colon = inner.rfind(':');
            bool digits = colon != std::string::npos && colon + 1 < inner.size();
            for (size_t i = colon + 1; digits && i < inner.size(); ++i)
                if (inner[i] < '0' || inner[i] > '9')
                    digits = false;
            if (digits)
                msg.resize(meta);
        }
        return msg;
    }

    /**
     * @brief 写入指定文本至目标文件
     *
     * @param path 文件路径
     * @param text 写入内容
     */
    void writeTestFile(const std::filesystem::path &path, const std::string &text)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    // =========================================================================
    // 测试用例实现
    // =========================================================================

    /**
     * @brief 测试控制字符与 ANSI 转义防注入功能
     */
    void testEscape()
    {
        section("escape / injection");
        useCaptureOutput();

        mylog::info("line1\nline2\x1b[31mred\x1b"
                    "[0m\ttab\rcr\x7f"
                    "del\x01"
                    "ctl");
        const std::string out = captured();

        CHECK(out.find("line1\\nline2\\e[31mred\\e[0m") != std::string::npos);
        CHECK(out.find("\\x09tab") != std::string::npos);
        CHECK(out.find("\\rcr") != std::string::npos);
        CHECK(out.find("\\x7f") != std::string::npos);
        CHECK(out.find("\\x01") != std::string::npos);
        CHECK(out.find('\x1b') == std::string::npos);
        CHECK(std::count(out.begin(), out.end(), '\n') == 1);
        CHECK(std::count(out.begin(), out.end(), '\r') == 0);
        CHECK(out.find('\x7f') == std::string::npos);
        CHECK(out.find('\t') == std::string::npos);

        CHECK(lineWellFormed(out));

        clearCaptured();
        mylog::info("plain-ascii-ok");
        CHECK(messageOf(captured()) == "plain-ascii-ok");
    }

    /**
     * @brief 测试运行时日志级别抑制与快速过滤
     */
    void testSuppress()
    {
        section("suppress level");
        useCaptureOutput();
        mylog::setSuppressLevel(mylog::LEVEL::WARN);
        CHECK(mylog::getCurrentSuppressLevel() == mylog::LEVEL::WARN);
        mylog::trace("t");
        mylog::debug("d");
        mylog::info("i");
        CHECK_EQ(captured(), std::string());
        mylog::warn("w");
        CHECK(captured().find("w") != std::string::npos);
        mylog::error("e");
        CHECK(captured().find("e") != std::string::npos);

        mylog::setSuppressLevel(mylog::LEVEL::TRACE);
    }

    /**
     * @brief 测试单条日志长度截断保护
     */
    void testMessageLengthCap()
    {
        section("message length cap");
        useCaptureOutput();

        const size_t cap = mylog::Logger::getInstance().getConfig().maxLogLength;
        const std::string huge(cap * 2 + 4096, 'x');
        mylog::info("{}", huge);
        const std::string out = captured();
        const std::string msg = messageOf(out);
        CHECK(msg.size() == cap);
    }

    /**
     * @brief 测试 Logger 运行时动态配置可调参数与边界钳制
     */
    void testLoggerTunables()
    {
        section("logger config tunables");
        useCaptureOutput();

        mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
        cfg.maxLogLength = 128;
        mylog::Logger::getInstance().setConfig(cfg);
        CHECK(mylog::Logger::getInstance().getConfig().maxLogLength == 128);

        const std::string huge(4096, 'y');
        clearCaptured();
        mylog::info("{}", huge);
        CHECK(messageOf(captured()).size() == 128);

        cfg = mylog::Logger::getInstance().getConfig();
        cfg.maxLogLength = 1;
        mylog::Logger::getInstance().setConfig(cfg);
        CHECK(mylog::Logger::getInstance().getConfig().maxLogLength == mylog::detail::kMinMaxLogLength);

        cfg = mylog::Logger::getInstance().getConfig();
        cfg.maxLogLength = static_cast<size_t>(-1);
        mylog::Logger::getInstance().setConfig(cfg);
        CHECK(mylog::Logger::getInstance().getConfig().maxLogLength == mylog::detail::kMaxMaxLogLength);

        cfg = mylog::Logger::getInstance().getConfig();
        cfg.timestampUtc = true;
        mylog::Logger::getInstance().setConfig(cfg);
        CHECK(mylog::Logger::getInstance().getConfig().timestampUtc);
        clearCaptured();
        mylog::info("utc");
        CHECK(lineWellFormed(captured()));

        cfg = mylog::Logger::getInstance().getConfig();
        cfg.timestampUtc = false;
        mylog::Logger::getInstance().setConfig(cfg);
        CHECK(!mylog::Logger::getInstance().getConfig().timestampUtc);
        clearCaptured();
        mylog::info("local");
        CHECK(lineWellFormed(captured()));

        std::string utcStamp, localStamp, noMs;
        mylog::detail::appendTimestamp(utcStamp, true, true);
        mylog::detail::appendTimestamp(localStamp, true, false);
        mylog::detail::appendTimestamp(noMs, false, false);
        CHECK(utcStamp.size() == 23);
        CHECK(localStamp.size() == 23);
        CHECK(noMs.size() == 19);

        cfg = mylog::Logger::getInstance().getConfig();
        cfg.maxLogLength = 1ULL << 20;
        mylog::Logger::getInstance().setConfig(cfg);
    }

    std::atomic<int> g_reentrantCalls{0};

    void reentrantOutput(void * /*ctx*/, std::string_view msg)
    {
        g_reentrantCalls.fetch_add(1);
        captureOutput(nullptr, msg);
        mylog::info("from-inside-callback");
    }

    /**
     * @brief 测试递归重入日志保护（防止回调死锁或栈溢出）
     */
    void testReentrancy()
    {
        section("reentrancy guard");
        mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
        cfg.outputFunction = mylog::OutputFunction{&reentrantOutput, nullptr};
        cfg.flushFunction = mylog::FlushFunction{&noFlush, nullptr};
        cfg.fatalAction = mylog::FATAL_ACTION::NONE;
        mylog::Logger::getInstance().setConfig(cfg);
        clearCaptured();
        g_reentrantCalls.store(0);

        mylog::info("outer");
        CHECK_EQ(g_reentrantCalls.load(), 1);
        CHECK(captured().find("outer") != std::string::npos);
        CHECK(captured().find("from-inside-callback") == std::string::npos);

        useCaptureOutput();
    }

    /**
     * @brief 测试多线程并发写入共享单文件的完整性与正确性
     */
    void testMultiThreadIntegrity(size_t threadBufferCapacity, const char *basename, const char *label)
    {
        section(label);
        constexpr unsigned kThreads = 4;
        constexpr size_t kPerThread = 5000;

        setFileConfig(basename, threadBufferCapacity, 1ULL << 30, 0);
        useFileOutput();
        mylog::FileManager::getInstance().flushFile();

        std::vector<std::thread> pool;
        pool.reserve(kThreads);
        for (unsigned t = 0; t < kThreads; ++t)
        {
            pool.emplace_back(
                [t]
                {
                    for (size_t i = 0; i < kPerThread; ++i)
                        LOG_INFO("t{}-i{}", t, i);
                });
        }
        for (auto &th : pool)
            th.join();
        mylog::FileManager::getInstance().flushFile();

        const std::vector<std::string> lines = readAllLines(basename);
        CHECK_EQ(lines.size(), static_cast<size_t>(kThreads) * kPerThread);

        std::unordered_map<std::string, int> seen;
        bool allWellFormed = true;
        for (const std::string &line : lines)
        {
            if (!lineWellFormed(line))
                allWellFormed = false;
            ++seen[messageOf(line)];
        }
        CHECK(allWellFormed);
        CHECK_EQ(seen.size(), static_cast<size_t>(kThreads) * kPerThread);

        bool complete = true;
        for (unsigned t = 0; t < kThreads && complete; ++t)
            for (size_t i = 0; i < kPerThread; ++i)
                if (seen["t" + std::to_string(t) + "-i" + std::to_string(i)] != 1)
                {
                    complete = false;
                    break;
                }
        CHECK(complete);
    }

    /**
     * @brief 测试线程退出时 TLS 缓冲区的自动排空与显式刷新
     */
    void testThreadBufferDrain()
    {
        section("thread buffer drain on exit");
        setFileConfig("drain", 1ULL << 20, 1ULL << 30, 0);
        useFileOutput();
        mylog::FileManager::getInstance().flushFile();

        std::thread worker(
            []
            {
                for (int i = 0; i < 50; ++i)
                    LOG_INFO("drain-{}", i);
            });
        worker.join();

        const std::vector<std::string> stillBuffered = readAllLines("drain");
        mylog::FileManager::getInstance().flushFile();

        const std::vector<std::string> lines = readAllLines("drain");
        CHECK_EQ(lines.size(), 50u);
        CHECK(stillBuffered.size() <= lines.size());
        std::unordered_map<std::string, int> seen;
        for (const std::string &line : lines)
            ++seen[messageOf(line)];
        CHECK_EQ(seen.size(), 50u);
        CHECK_EQ(seen["drain-49"], 1);
    }

    /**
     * @brief 测试日志容量滚动与 maxFiles 保留策略
     */
    void testRollingAndRetention()
    {
        section("rolling + maxFiles");
        constexpr size_t kThreadBuffer = 4096;
        constexpr size_t kFileCapacity = 8192;
        constexpr size_t kMaxFiles = 3;

        setFileConfig("roll", kThreadBuffer, kFileCapacity, kMaxFiles);
        useFileOutput();
        mylog::FileManager::getInstance().flushFile();

        for (int i = 0; i < 3000; ++i)
            mylog::info("roll-message-{:06d}-padding-padding-padding", i);
        mylog::FileManager::getInstance().flushFile();

        const std::vector<std::filesystem::path> files = collectLogs("roll");
        CHECK(!files.empty());
        CHECK(files.size() <= kMaxFiles);

        size_t totalBytes = 0;
        bool sizeBounded = true;
        for (const auto &path : files)
        {
            const auto size = static_cast<size_t>(std::filesystem::file_size(path));
            totalBytes += size;
            if (size > kFileCapacity + 2 * kThreadBuffer)
                sizeBounded = false;
        }
        CHECK(sizeBounded);
        CHECK(totalBytes > 0);

        if (files.size() >= 2)
            CHECK(files.front().filename().string() < files.back().filename().string());
    }

    /**
     * @brief 测试高并发写入下日志滚动与行完整性（无撕裂）
     */
    void testConcurrentRolling()
    {
        section("concurrent rolling (line integrity)");
        constexpr unsigned kThreads = 4;
        constexpr size_t kPerThread = 4000;
        constexpr size_t kThreadBuffer = 4096;
        constexpr size_t kFileCapacity = 16384;
        constexpr size_t kMaxFiles = 4;

        setFileConfig("croll", kThreadBuffer, kFileCapacity, kMaxFiles);
        useFileOutput();
        mylog::FileManager::getInstance().flushFile();

        std::vector<std::thread> pool;
        pool.reserve(kThreads);
        for (unsigned t = 0; t < kThreads; ++t)
        {
            pool.emplace_back(
                [t]
                {
                    for (size_t i = 0; i < kPerThread; ++i)
                        mylog::info("c{}-{:05d}-padpadpadpadpadpadpadpad", t, i);
                });
        }
        for (auto &th : pool)
            th.join();
        mylog::FileManager::getInstance().flushFile();

        const std::vector<std::filesystem::path> files = collectLogs("croll");
        CHECK(!files.empty());
        CHECK(files.size() <= kMaxFiles);

        size_t lineCount = 0;
        bool allWellFormed = true;
        for (const auto &path : files)
        {
            const std::string text = readText(path);
            size_t start = 0;
            while (start < text.size())
            {
                const size_t end = text.find('\n', start);
                if (end == std::string::npos)
                {
                    allWellFormed = false;
                    break;
                }
                const std::string line = text.substr(start, end - start + 1);
                if (!lineWellFormed(line))
                    allWellFormed = false;
                ++lineCount;
                start = end + 1;
            }
        }
        CHECK(allWellFormed);
        CHECK(lineCount > 0);
    }

    /**
     * @brief 测试每线程独占文件模式的写入完整性与独立性
     */
    void testPerThreadFileIntegrity(size_t threadBufferCapacity, const char *basename, const char *label)
    {
        section(label);
        constexpr unsigned kThreads = 4;
        constexpr size_t kPerThread = 5000;

        setFileConfig(basename, threadBufferCapacity, 1ULL << 30, 0);
        useFileOutputPerThread();

        std::vector<std::thread> pool;
        pool.reserve(kThreads);
        for (unsigned t = 0; t < kThreads; ++t)
        {
            pool.emplace_back(
                [t]
                {
                    for (size_t i = 0; i < kPerThread; ++i)
                        LOG_INFO("p{}-i{}", t, i);
                });
        }
        for (auto &th : pool)
            th.join();

        const std::vector<std::filesystem::path> files = collectPerThreadLogs(basename);
        CHECK_EQ(files.size(), static_cast<size_t>(kThreads));

        const std::vector<std::string> lines = readLinesFrom(files);
        CHECK_EQ(lines.size(), static_cast<size_t>(kThreads) * kPerThread);

        std::unordered_map<std::string, int> seen;
        bool allWellFormed = true;
        for (const std::string &line : lines)
        {
            if (!lineWellFormed(line))
                allWellFormed = false;
            ++seen[messageOf(line)];
        }
        CHECK(allWellFormed);
        CHECK_EQ(seen.size(), static_cast<size_t>(kThreads) * kPerThread);

        bool complete = true;
        for (unsigned t = 0; t < kThreads && complete; ++t)
            for (size_t i = 0; i < kPerThread; ++i)
                if (seen["p" + std::to_string(t) + "-i" + std::to_string(i)] != 1)
                {
                    complete = false;
                    break;
                }
        CHECK(complete);

        bool perFileComplete = true;
        for (const auto &path : files)
        {
            const std::string text = readText(path);
            size_t n = 0;
            bool formed = true;
            size_t start = 0;
            while (start < text.size())
            {
                const size_t end = text.find('\n', start);
                if (end == std::string::npos)
                {
                    formed = false;
                    break;
                }
                if (!lineWellFormed(text.substr(start, end - start + 1)))
                    formed = false;
                ++n;
                start = end + 1;
            }
            if (!formed || n != kPerThread)
                perFileComplete = false;
        }
        CHECK(perFileComplete);
    }

    /**
     * @brief 测试每线程独占文件模式的容量滚动与保留清理
     */
    void testPerThreadFileRolling()
    {
        section("per-thread files rolling + maxFiles");
        constexpr size_t kFileCapacity = 8192;
        constexpr size_t kMaxFiles = 2;
        constexpr size_t kMessages = 3000;

        setFileConfig("ptr", 0, kFileCapacity, kMaxFiles);
        useFileOutputPerThread();

        std::thread worker(
            []
            {
                for (size_t i = 0; i < kMessages; ++i)
                    mylog::info("ptr-{:06d}-padding-padding-padding", i);
            });
        worker.join();

        const std::vector<std::filesystem::path> files = collectPerThreadLogs("ptr");
        CHECK(!files.empty());
        CHECK(files.size() > 1);
        CHECK(files.size() <= kMaxFiles);

        bool allWellFormed = true;
        size_t lineCount = 0;
        size_t oversize = 0;
        for (const auto &path : files)
        {
            const auto size = static_cast<size_t>(std::filesystem::file_size(path));
            if (size > kFileCapacity + 4096)
                ++oversize;
            const std::string text = readText(path);
            size_t start = 0;
            while (start < text.size())
            {
                const size_t end = text.find('\n', start);
                if (end == std::string::npos)
                {
                    allWellFormed = false;
                    break;
                }
                if (!lineWellFormed(text.substr(start, end - start + 1)))
                    allWellFormed = false;
                ++lineCount;
                start = end + 1;
            }
        }
        CHECK(allWellFormed);
        CHECK(oversize == 0);
        CHECK(lineCount > 0);
        CHECK(lineCount <= kMessages);
    }

    /**
     * @brief 测试控制字符转义动态开关
     */
    void testEscapeToggle()
    {
        section("escapeControlChars toggle");

        useCaptureOutput();
        mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
        CHECK(cfg.escapeControlChars);

        mylog::info("a\nb");
        CHECK(captured().find("a\\nb") != std::string::npos);

        cfg.escapeControlChars = false;
        mylog::Logger::getInstance().setConfig(cfg);
        clearCaptured();
        mylog::info("c\nd");
        CHECK(captured().find("c\nd") != std::string::npos);

        cfg.escapeControlChars = true;
        mylog::Logger::getInstance().setConfig(cfg);
    }

    /**
     * @brief 测试日志文件基名（basename）安全性校验
     */
    void testBasenameValidation()
    {
        section("basename validation");
        CHECK(mylog::FileManager::isValidBasename("app"));
        CHECK(mylog::FileManager::isValidBasename("my-app_01.log"));
        CHECK(mylog::FileManager::isValidBasename("123"));
        CHECK(!mylog::FileManager::isValidBasename(""));
        CHECK(!mylog::FileManager::isValidBasename("."));
        CHECK(!mylog::FileManager::isValidBasename(".."));
        CHECK(!mylog::FileManager::isValidBasename("../evil"));
        CHECK(!mylog::FileManager::isValidBasename("app/sub"));
        CHECK(!mylog::FileManager::isValidBasename("app\\sub"));
        CHECK(!mylog::FileManager::isValidBasename("app name"));

        const std::string tooLong(129, 'a');
        CHECK(!mylog::FileManager::isValidBasename(tooLong));
        const std::string exact128(128, 'a');
        CHECK(mylog::FileManager::isValidBasename(exact128));
    }

    /**
     * @brief 测试 Lua 动态配置加载、反射解析与沙箱命令注入防护
     */
    void testLuaConfig()
    {
        section("lua config");
        const std::filesystem::path good = "config_good.lua";
        const mylog::Logger::Config savedCfg = mylog::Logger::getInstance().getConfig();
        writeTestFile(good, "logger = {\n"
                            "  suppressLevel = \"warn\",\n"
                            "  stdoutVivid = true,\n"
                            "  escapeControlChars = false,\n"
                            "  timestampWithMs = false,\n"
                            "  timestampUtc = true,\n"
                            "  maxLogLength = 4096,\n"
                            "  fatalAction = \"none\"\n"
                            "}\n"
                            "fileManager = {\n"
                            "  basename = \"luaapp\",\n"
                            "  fileCapacity = 123456,\n"
                            "  threadBufferCapacity = 1111,\n"
                            "  intervalFlushFile_ms = 2000,\n"
                            "  maxFiles = 5\n"
                            "}\n");
        CHECK(mylog::reloadConfig(good));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::WARN));
        const mylog::Logger::Config lcfg = mylog::Logger::getInstance().getConfig();
        CHECK(lcfg.lv2str == &mylog::LV2STR_VIVID);
        CHECK(!lcfg.escapeControlChars);
        CHECK(!lcfg.timestampWithMs);
        CHECK(lcfg.timestampUtc);
        CHECK_EQ(lcfg.maxLogLength, 4096u);
        CHECK(lcfg.fatalAction == mylog::FATAL_ACTION::NONE);
        const mylog::FileManager::Config fcfg = mylog::FileManager::getInstance().getConfig();
        CHECK_EQ(fcfg.basename, std::string("luaapp"));
        CHECK_EQ(fcfg.fileCapacity, 123456u);
        CHECK_EQ(fcfg.threadBufferCapacity, 1111u);
        CHECK_EQ(fcfg.intervalFlushFile_ms, 2000u);
        CHECK_EQ(fcfg.maxFiles, 5u);

        const std::filesystem::path clampCfg = "config_clamp.lua";
        writeTestFile(clampCfg, "logger = {\n"
                                "  maxLogLength = 1\n"
                                "}\n");
        CHECK(mylog::reloadConfig(clampCfg));
        const mylog::Logger::Config clamped = mylog::Logger::getInstance().getConfig();
        CHECK_EQ(clamped.maxLogLength, mylog::detail::kMinMaxLogLength);
        mylog::Logger::getInstance().setConfig(savedCfg);

        const std::filesystem::path bad = "config_bad.lua";
        writeTestFile(bad, "fileManager = { basename = \"../evil\" }\n");
        CHECK(!mylog::reloadConfig(bad));
        CHECK_EQ(mylog::FileManager::getInstance().getConfig().basename, std::string("luaapp"));

        const std::filesystem::path intLevel = "config_int_level.lua";
        writeTestFile(intLevel, "logger = { suppressLevel = 0 }\n");
        CHECK(!mylog::reloadConfig(intLevel));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::WARN));

        const std::filesystem::path badLevel = "config_bad_level.lua";
        writeTestFile(badLevel, "logger = { suppressLevel = 99 }\n");
        CHECK(!mylog::reloadConfig(badLevel));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::WARN));

        const std::filesystem::path strLevel = "config_str_level.lua";
        writeTestFile(strLevel, "logger = { suppressLevel = \"info\" }\n");
        CHECK(mylog::reloadConfig(strLevel));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::INFO));

        const std::filesystem::path badStrLevel = "config_bad_str_level.lua";
        writeTestFile(badStrLevel, "logger = { suppressLevel = \"nonexistent\" }\n");
        CHECK(!mylog::reloadConfig(badStrLevel));
        CHECK_EQ(static_cast<int>(mylog::getCurrentSuppressLevel()), static_cast<int>(mylog::LEVEL::INFO));

        const std::filesystem::path broken = "config_broken.lua";
        writeTestFile(broken, "logger = { suppressLevel = \n");
        CHECK(!mylog::reloadConfig(broken));

        const std::filesystem::path evil = "config_evil.lua";
        const std::string marker = "/tmp/mylog_pwned_marker";
        std::error_code ec;
        std::filesystem::remove(marker, ec);
        writeTestFile(evil, "os.execute(\"touch " + marker + "\")\n");
        CHECK(!mylog::reloadConfig(evil));
        CHECK(!std::filesystem::exists(marker));

        mylog::Logger::getInstance().setConfig(savedCfg);

        std::filesystem::remove(good, ec);
        std::filesystem::remove(bad, ec);
        std::filesystem::remove(intLevel, ec);
        std::filesystem::remove(badLevel, ec);
        std::filesystem::remove(strLevel, ec);
        std::filesystem::remove(badStrLevel, ec);
        std::filesystem::remove(clampCfg, ec);
        std::filesystem::remove(broken, ec);
        std::filesystem::remove(evil, ec);
    }

    /**
     * @brief 测试 FATAL 动作（默认 NONE 不退出，配置 ABORT 触发崩溃信号）
     */
    void testFatalAction()
    {
        section("FATAL action");
        CHECK(mylog::Logger::Config{}.fatalAction == mylog::FATAL_ACTION::NONE);
        useCaptureOutput();
        mylog::fatal("fatal-none");
        CHECK(captured().find("fatal-none") != std::string::npos);

        struct rlimit rl{};
        rl.rlim_cur = 0;
        rl.rlim_max = 0;
        ::setrlimit(RLIMIT_CORE, &rl);

        const pid_t pid = ::fork();
        if (pid == 0)
        {
            mylog::Logger::Config abortCfg = mylog::Logger::getInstance().getConfig();
            abortCfg.fatalAction = mylog::FATAL_ACTION::ABORT;
            mylog::Logger::getInstance().setConfig(std::move(abortCfg));
            mylog::fatal("fatal-abort");
            ::_exit(0);
        }
        CHECK(pid > 0);
        int status = 0;
        ::waitpid(pid, &status, 0);
        CHECK(WIFSIGNALED(status));
        CHECK_EQ(WTERMSIG(status), SIGABRT);

        useCaptureOutput();
    }

    struct TestUser
    {
        std::string name;
        int age;
        double score;
    };

    enum class TestColor
    {
        Red,
        Green,
        Blue
    };

    /**
     * @brief 测试 C++26 静态反射（枚举反查、结构体展开、敏感脱敏、别名一致性）
     */
    void testReflection()
    {
        section("C++26 reflection & formatters");
        useCaptureOutput();

        // 1. 枚举成员数量反射查询（动词规范与兼容别名）
        CHECK_EQ(mylog::reflect::countEnumMembers<mylog::LEVEL>(), 6u);
        CHECK_EQ(mylog::reflect::enum_count<mylog::LEVEL>(), 6u);
        CHECK_EQ(mylog::reflect::countEnumMembers<mylog::FATAL_ACTION>(), 3u);
        CHECK_EQ(mylog::reflect::countEnumMembers<TestColor>(), 3u);

        CHECK_EQ(static_cast<uint8_t>(mylog::FATAL_ACTION::NONE), 0u);
        CHECK_EQ(static_cast<uint8_t>(mylog::FATAL_ACTION::EXIT), 1u);
        CHECK_EQ(static_cast<uint8_t>(mylog::FATAL_ACTION::ABORT), 2u);

        // 2. 枚举值转字符串（动词规范与兼容别名）
        CHECK_EQ(mylog::reflect::enumToString(mylog::LEVEL::TRACE), std::string_view("TRACE"));
        CHECK_EQ(mylog::reflect::enum_to_string(mylog::LEVEL::TRACE), std::string_view("TRACE"));
        CHECK_EQ(mylog::reflect::enumToString(mylog::LEVEL::INFO), std::string_view("INFO"));
        CHECK_EQ(mylog::reflect::enumToString(mylog::LEVEL::FATAL), std::string_view("FATAL"));
        CHECK_EQ(mylog::reflect::enumToString(TestColor::Green), std::string_view("Green"));

        // 3. 枚举反查解析（动词规范与兼容别名）
        auto parsedAct = mylog::reflect::parseEnum<mylog::FATAL_ACTION>("abort");
        CHECK(parsedAct.has_value());
        CHECK(*parsedAct == mylog::FATAL_ACTION::ABORT);

        auto parsedActUpper = mylog::reflect::parse_enum<mylog::FATAL_ACTION>("EXIT");
        CHECK(parsedActUpper.has_value());
        CHECK(*parsedActUpper == mylog::FATAL_ACTION::EXIT);

        auto parsedLevel = mylog::reflect::parseEnum<mylog::LEVEL>("warn");
        CHECK(parsedLevel.has_value());
        CHECK(*parsedLevel == mylog::LEVEL::WARN);

        auto badLevel = mylog::reflect::parseEnum<mylog::LEVEL>("nonexistent");
        CHECK(!badLevel.has_value());

        // 4. 结构体成员数量与反射元信息数组（动词规范与兼容别名）
        CHECK_EQ(mylog::reflect::countMembers<TestUser>(), 3u);
        CHECK_EQ(mylog::reflect::member_count<TestUser>(), 3u);
        CHECK_EQ(mylog::reflect::countMembers<mylog::FileManager::Config>(), 6u);
        static_assert(mylog::reflect::getMemberInfos<TestUser>().size() == 3u);
        static_assert(mylog::reflect::get_member_infos<TestUser>().size() == 3u);

        // 5. 聚合结构体自动格式化输出
        TestUser u{"Alice", 25, 99.5};
        clearCaptured();
        mylog::info("user={}", u);
        CHECK(captured().find("user={name: Alice, age: 25, score: 99.5}") != std::string::npos);

        // 6. 作用域枚举自动格式化输出
        clearCaptured();
        mylog::info("color={}", TestColor::Blue);
        CHECK(captured().find("color=Blue") != std::string::npos);

        clearCaptured();
        mylog::info("level={}", mylog::LEVEL::DEBUG);
        CHECK(captured().find("level=DEBUG") != std::string::npos);

        // 7. 敏感字段注解 [[= mylog::sensitive]] 自动掩码脱敏
        struct TestAccount
        {
            std::string username;
            [[= mylog::sensitive]] std::string password;
            [[= mylog::sensitive]] std::string token;
            int roleId;
        };
        TestAccount acc{"alice", "secret_pass_123", "token_xyz_999", 1};
        clearCaptured();
        mylog::info("account: {}", acc);
        CHECK(captured().find("account: {username: alice, password: \"******\", token: \"******\", roleId: 1}") !=
              std::string::npos);
        CHECK(captured().find("secret_pass_123") == std::string::npos);
        CHECK(captured().find("token_xyz_999") == std::string::npos);

        // 8. 辅助级别/动作转换函数
        CHECK_EQ(mylog::levelToString(mylog::LEVEL::INFO), "INFO");
        CHECK_EQ(mylog::levelToString(mylog::LEVEL::ERROR), "ERROR");
        CHECK(mylog::parseLevel("trace") == mylog::LEVEL::TRACE);
        CHECK(mylog::parseLevel("FATAL") == mylog::LEVEL::FATAL);
        CHECK(!mylog::parseLevel("unknown").has_value());

        CHECK_EQ(mylog::fatalActionToString(mylog::FATAL_ACTION::ABORT), "ABORT");
        CHECK(mylog::parseFatalAction("exit") == mylog::FATAL_ACTION::EXIT);
        CHECK(mylog::parseFatalAction("NONE") == mylog::FATAL_ACTION::NONE);
        CHECK(!mylog::parseFatalAction("unknown").has_value());

        CHECK(mylog::setSuppressLevel("DEBUG"));
        CHECK_EQ(mylog::getCurrentSuppressLevel(), mylog::LEVEL::DEBUG);
        CHECK(!mylog::setSuppressLevel("invalid_level"));
        CHECK_EQ(mylog::getCurrentSuppressLevel(), mylog::LEVEL::DEBUG);
    }

    /**
     * @brief 测试现代 C++26 基于 std::source_location 的无宏日志 API
     */
    void testMacroFreeLogging()
    {
        section("C++26 macro-free logging with source_location");
        useCaptureOutput();
        mylog::setSuppressLevel(mylog::LEVEL::TRACE);

        // 1. mylog::info 格式化重载与文件名/行号注入
        clearCaptured();
        const int expectedLine1 = __LINE__ + 1;
        mylog::info("hello {} and {}", "modern", 2026);
        std::string out1 = captured();
        CHECK(out1.find("hello modern and 2026") != std::string::npos);
        CHECK(out1.find("test.cpp:" + std::to_string(expectedLine1)) != std::string::npos);
        CHECK(out1.find("INFO") != std::string::npos);

        // 2. mylog::debug 纯文本重载
        clearCaptured();
        const int expectedLine2 = __LINE__ + 1;
        mylog::debug("simple debug message");
        std::string out2 = captured();
        CHECK(out2.find("simple debug message") != std::string::npos);
        CHECK(out2.find("test.cpp:" + std::to_string(expectedLine2)) != std::string::npos);
        CHECK(out2.find("DEBUG") != std::string::npos);

        // 3. mylog::trace / mylog::warn / mylog::error
        clearCaptured();
        mylog::trace("trace log");
        CHECK(captured().find("TRACE") != std::string::npos);

        clearCaptured();
        mylog::warn("warning code: {:04d}", 42);
        CHECK(captured().find("warning code: 0042") != std::string::npos);
        CHECK(captured().find("WARN") != std::string::npos);

        clearCaptured();
        mylog::error("error msg: {}", "failed to connect");
        CHECK(captured().find("error msg: failed to connect") != std::string::npos);
        CHECK(captured().find("ERROR") != std::string::npos);

        // 4. 对象原位打印重载：mylog::info(obj)
        struct SimplePoint
        {
            int x;
            int y;
        };
        clearCaptured();
        mylog::info(SimplePoint{10, 20});
        CHECK(captured().find("{x: 10, y: 20}") != std::string::npos);

        // 5. 无宏 API 级别抑制检查
        clearCaptured();
        mylog::setSuppressLevel(mylog::LEVEL::WARN);
        mylog::info("this should be suppressed");
        mylog::debug("this should also be suppressed");
        CHECK(captured().empty());
        mylog::warn("this should appear");
        CHECK(captured().find("this should appear") != std::string::npos);
        mylog::setSuppressLevel(mylog::LEVEL::TRACE);
    }

} // namespace

int main()
{
    char tmpl[] = "/tmp/mylog_test_XXXXXX";
    const char *dir = ::mkdtemp(tmpl);
    if (dir == nullptr)
    {
        std::fprintf(stderr, "mkdtemp failed\n");
        return 2;
    }
    if (::chdir(dir) != 0)
    {
        std::fprintf(stderr, "chdir failed\n");
        return 2;
    }
    std::fprintf(stderr, "work dir: %s\n", dir);

    testEscape();
    testSuppress();
    testMessageLengthCap();
    testLoggerTunables();
    testReentrancy();
    testMultiThreadIntegrity(65536, "mt_tls", "multithread + thread buffer");
    testMultiThreadIntegrity(0, "mt_nobuf", "multithread + shared buffer (thread buffer off)");
    testThreadBufferDrain();
    testRollingAndRetention();
    testConcurrentRolling();
    testPerThreadFileIntegrity(0, "ptf_nobuf", "per-thread files, unbuffered");
    testPerThreadFileIntegrity(65536, "ptf_tls", "per-thread files, thread buffer");
    testPerThreadFileRolling();
    testEscapeToggle();
    testBasenameValidation();
    testLuaConfig();
    testFatalAction();
    testReflection();
    testMacroFreeLogging();

    std::fprintf(stderr, "\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
