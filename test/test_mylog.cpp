// mylog 测试：零依赖手写测试，不引入 gtest。
//
// 覆盖范围：
//   1. 控制字符 / ANSI 转义（日志注入防护）
//   2. 运行时抑制级别
//   3. 多线程写入完整性（开启 / 关闭每线程缓冲两条路径）
//   4. 线程缓冲排空（线程退出、显式 flush）
//   5. 按容量滚动与 maxFiles 保留策略
//   6. 单条长度上限
//   7. 重入防护
//   8. FATAL 动作（NONE 不退出 / ABORT 触发 SIGABRT）
//   9. basename 校验与 Lua 配置加载（含「配置脚本不得执行命令」的安全断言）

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

#define CHECK(cond)                                                                              \
    do                                                                                           \
    {                                                                                            \
        ++g_checks;                                                                              \
        if (!(cond))                                                                             \
        {                                                                                        \
            ++g_failures;                                                                        \
            std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                           \
    do                                                                                           \
    {                                                                                            \
        ++g_checks;                                                                              \
        if (!((a) == (b)))                                                                       \
        {                                                                                        \
            ++g_failures;                                                                        \
            std::fprintf(stderr, "  FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b);        \
        }                                                                                        \
    } while (0)

void section(const char *name)
{
    std::fprintf(stderr, "\n[%s]\n", name);
}

std::mutex g_captureMtx;
std::string g_capture;

// 回调签名是「函数指针 + ctx」，所以这里不再用捕获 lambda：
// ctx 为空时就直接操作文件级状态。
void captureOutput(void * /*ctx*/, std::string_view msg)
{
    std::lock_guard<std::mutex> lock(g_captureMtx);
    g_capture.append(msg.data(), msg.size());
}

void noFlush(void * /*ctx*/) {}

// 把「成员函数 + 对象」绑成零分配回调：测试里大量需要带状态的收集器。
template <auto Method, typename T>
mylog::OutputFunction bindOutput(T *obj)
{
    return mylog::OutputFunction{
        [](void *ctx, std::string_view msg) { (static_cast<T *>(ctx)->*Method)(msg); }, obj};
}

template <auto Method, typename T>
mylog::FlushFunction bindFlush(T *obj)
{
    return mylog::FlushFunction{[](void *ctx) { (static_cast<T *>(ctx)->*Method)(); }, obj};
}

std::string captured()
{
    std::lock_guard<std::mutex> lock(g_captureMtx);
    return g_capture;
}

void clearCaptured()
{
    std::lock_guard<std::mutex> lock(g_captureMtx);
    g_capture.clear();
}

// 让日志走内存回调，便于断言内容
void useCaptureOutput()
{
    mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
    cfg.outputFunction = mylog::OutputFunction{&captureOutput, nullptr};
    cfg.flushFunction = mylog::FlushFunction{&noFlush, nullptr};
    cfg.fatalAction = mylog::FATAL_ACTION::NONE;
    mylog::Logger::getInstance().setConfig(cfg);
    mylog::Logger::getInstance().setSuppressLevel(mylog::LEVEL::TRACE);
    clearCaptured();
}

void useFileOutput()
{
    mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
    cfg.outputFunction = mylog::FileManager::outputFunction_file();
    cfg.flushFunction = mylog::FileManager::flushFunction_file();
    cfg.fatalAction = mylog::FATAL_ACTION::NONE;
    mylog::Logger::getInstance().setConfig(cfg);
}

void useFileOutputPerThread()
{
    mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
    cfg.outputFunction = mylog::FileManager::outputFunction_filePerThread();
    cfg.flushFunction = mylog::FileManager::flushFunction_filePerThread();
    cfg.fatalAction = mylog::FATAL_ACTION::NONE;
    mylog::Logger::getInstance().setConfig(cfg);
}

void setFileConfig(std::string basename, size_t threadBuffer, size_t fileCapacity,
                   size_t maxFiles)
{
    mylog::FileManager::Config fcfg = mylog::FileManager::getInstance().getConfig();
    fcfg.basename = std::move(basename);
    fcfg.threadBufferCapacity = threadBuffer;
    fcfg.fileCapacity = fileCapacity;
    fcfg.maxFiles = maxFiles;
    mylog::FileManager::getInstance().setConfig(fcfg);
}

std::string pidSuffix(const std::string &basename)
{
    return "_" + basename + "_" + std::to_string(static_cast<long>(::getpid())) + ".log";
}

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

// A2（每线程独立文件）的文件名形如 <ts>_<basename>_<pid>_<tid>.log；
// 共享单文件形如 <ts>_<basename>_<pid>.log，不含末尾的 _<tid>，两者天然不重叠。
std::string perThreadInfix(const std::string &basename)
{
    return "_" + basename + "_" + std::to_string(static_cast<long>(::getpid())) + "_";
}

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
        if (name.size() > infix.size() + 4 && name.find(infix) != std::string::npos &&
            name.ends_with(".log"))
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string readText(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// 回读给定分片，返回按行拆分的结果（最后一行可能残缺，交由调用方断言）
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
                lines.push_back(text.substr(start)); // 残缺行，交由调用方断言
                break;
            }
            lines.push_back(text.substr(start, end - start + 1));
            start = end + 1;
        }
    }
    return lines;
}

// 回读所有分片，返回按行拆分的结果
std::vector<std::string> readAllLines(const std::string &basename)
{
    return readLinesFrom(collectLogs(basename));
}

// 一行日志是否结构完整：以 "YYYY-MM-DD HH:MM:SS.mmm " 开头、以 '\n' 结尾
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
    // 毫秒：." + 3 位数字 + 空格
    return line[19] == '.' && line[23] == ' ' && line[20] >= '0' && line[20] <= '9' &&
           line[21] >= '0' && line[21] <= '9' && line[22] >= '0' && line[22] <= '9';
}

// 取出日志正文：行格式为 `... [tid]func>>message (file:line)\n`（见 src/logger.hpp），
// 以第一个 ">>" 为界；func 名里不可能出现 '>'，故定位无歧义。
// 行尾的 ` (file:line)` 是元数据，需剥离；括号内必须是 `路径:十进制行号`，
// 因此正文自身以 " (xx:1)" 结尾也不会被误剥。
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

// ---------------- 测试用例 ----------------

void testEscape()
{
    section("escape / injection");
    useCaptureOutput();

    // 注意：\x 十六进制转义在 C++ 中是贪婪的，后接十六进制字符必须拆开写
    LOG_INFO("line1\nline2\x1b[31mred\x1b" "[0m\ttab\rcr\x7f" "del\x01" "ctl");
    const std::string out = captured();

    CHECK(out.find("line1\\nline2\\e[31mred\\e[0m") != std::string::npos);
    CHECK(out.find("\\x09tab") != std::string::npos); // tab 也转义，防用制表符伪造列
    CHECK(out.find("\\rcr") != std::string::npos);
    CHECK(out.find("\\x7f") != std::string::npos);   // DEL
    CHECK(out.find("\\x01") != std::string::npos);   // 其他 C0 控制字符
    CHECK(out.find('\x1b') == std::string::npos);     // 不得残留 ESC
    CHECK(std::count(out.begin(), out.end(), '\n') == 1); // 只允许行尾一个换行
    CHECK(std::count(out.begin(), out.end(), '\r') == 0);
    CHECK(out.find('\x7f') == std::string::npos);
    CHECK(out.find('\t') == std::string::npos);

    // 时间戳格式：19 位 + 毫秒
    CHECK(lineWellFormed(out));

    // 纯 ASCII 消息走快路径，内容必须逐字节保持
    clearCaptured();
    LOG_INFO("plain-ascii-ok");
    CHECK(messageOf(captured()) == "plain-ascii-ok");
}

void testSuppress()
{
    section("suppress level");
    useCaptureOutput();
    mylog::Logger::getInstance().setSuppressLevel(mylog::LEVEL::WARN);
    CHECK(mylog::Logger::getInstance().getSuppressLevel() == mylog::LEVEL::WARN);
    LOG_TRACE("t");
    LOG_DEBUG("d");
    LOG_INFO("i");
    CHECK_EQ(captured(), std::string());
    LOG_WARN("w");
    CHECK(captured().find("w") != std::string::npos);
    LOG_ERROR("e");
    CHECK(captured().find("e") != std::string::npos);

    mylog::Logger::getInstance().setSuppressLevel(mylog::LEVEL::TRACE);
}

void testMessageLengthCap()
{
    section("message length cap");
    useCaptureOutput();

    // 不再硬编码：阈值现在由 Logger::Config::maxLogLength 提供
    const size_t cap = mylog::Logger::getInstance().getConfig().maxLogLength;
    const std::string huge(cap * 2 + 4096, 'x');
    LOG_INFO("{}", huge);
    const std::string out = captured();
    const std::string msg = messageOf(out);
    CHECK(msg.size() == cap);
}

void testLoggerTunables()
{
    section("logger config tunables");
    useCaptureOutput();

    // —— maxLogLength：截断阈值可配 ——
    mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
    cfg.maxLogLength = 128;
    mylog::Logger::getInstance().setConfig(cfg);
    CHECK(mylog::Logger::getInstance().getConfig().maxLogLength == 128);

    const std::string huge(4096, 'y');
    clearCaptured();
    LOG_INFO("{}", huge);
    CHECK(messageOf(captured()).size() == 128);

    // 越界在发布咽喉里被钳制：Lua 与代码两条路都绕不过
    cfg = mylog::Logger::getInstance().getConfig();
    cfg.maxLogLength = 1; // 低于下界
    mylog::Logger::getInstance().setConfig(cfg);
    CHECK(mylog::Logger::getInstance().getConfig().maxLogLength == mylog::detail::kMinMaxLogLength);

    cfg = mylog::Logger::getInstance().getConfig();
    cfg.maxLogLength = static_cast<size_t>(-1); // 高于上界
    mylog::Logger::getInstance().setConfig(cfg);
    CHECK(mylog::Logger::getInstance().getConfig().maxLogLength == mylog::detail::kMaxMaxLogLength);

    // —— timestampUtc：开关能走到输出层，且时间戳布局不变 ——
    cfg = mylog::Logger::getInstance().getConfig();
    cfg.timestampUtc = true;
    mylog::Logger::getInstance().setConfig(cfg);
    CHECK(mylog::Logger::getInstance().getConfig().timestampUtc);
    clearCaptured();
    LOG_INFO("utc");
    CHECK(lineWellFormed(captured()));

    cfg = mylog::Logger::getInstance().getConfig();
    cfg.timestampUtc = false;
    mylog::Logger::getInstance().setConfig(cfg);
    CHECK(!mylog::Logger::getInstance().getConfig().timestampUtc);
    clearCaptured();
    LOG_INFO("local");
    CHECK(lineWellFormed(captured()));

    // 直接验证格式化本身：带毫秒 23 字节，不带 19 字节
    std::string utcStamp, localStamp, noMs;
    mylog::detail::appendTimestamp(utcStamp, true, true);
    mylog::detail::appendTimestamp(localStamp, true, false);
    mylog::detail::appendTimestamp(noMs, false, false);
    CHECK(utcStamp.size() == 23);
    CHECK(localStamp.size() == 23);
    CHECK(noMs.size() == 19);

    // 恢复默认，避免影响后续用例
    cfg = mylog::Logger::getInstance().getConfig();
    cfg.maxLogLength = 1u << 20;
    mylog::Logger::getInstance().setConfig(cfg);
}

std::atomic<int> g_reentrantCalls{0};

void reentrantOutput(void * /*ctx*/, std::string_view msg)
{
    g_reentrantCalls.fetch_add(1);
    captureOutput(nullptr, msg);
    LOG_INFO("from-inside-callback"); // 应被重入防护丢弃
}

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

    LOG_INFO("outer");
    CHECK_EQ(g_reentrantCalls.load(), 1);
    CHECK(captured().find("outer") != std::string::npos);
    CHECK(captured().find("from-inside-callback") == std::string::npos);

    useCaptureOutput();
}

void testMultiThreadIntegrity(size_t threadBufferCapacity, const char *basename,
                              const char *label)
{
    section(label);
    constexpr unsigned kThreads = 4;
    constexpr size_t kPerThread = 5000;

    setFileConfig(basename, threadBufferCapacity, 1ULL << 30, 0); // 容量足够大，测不滚动
    useFileOutput();
    mylog::FileManager::getInstance().flushFile(); // 触发切换到新 basename 的文件

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
        th.join(); // 线程退出应排空各自的 thread_local 缓冲
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

void testThreadBufferDrain()
{
    section("thread buffer drain on exit");
    setFileConfig("drain", 1u << 20, 1ULL << 30, 0); // 1 MiB 阈值，远大于写入量
    useFileOutput();
    mylog::FileManager::getInstance().flushFile();

    // 子线程写少量日志就退出：不足以触发阈值，必须靠线程退出时的排空逻辑落盘
    std::thread worker(
        []
        {
            for (int i = 0; i < 50; ++i)
                LOG_INFO("drain-{}", i);
        });
    worker.join();

    const std::vector<std::string> stillBuffered = readAllLines("drain");
    // 线程退出只把数据推进共享缓冲，真正落盘仍需 flush
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
        LOG_INFO("roll-message-{:06d}-padding-padding-padding", i);
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
        // 软上限：允许超出共享缓冲里尚未落盘的部分
        if (size > kFileCapacity + 2 * kThreadBuffer)
            sizeBounded = false;
    }
    CHECK(sizeBounded);
    CHECK(totalBytes > 0);

    // 保留的是最新的若干文件：文件名按时间戳排序
    if (files.size() >= 2)
        CHECK(files.front().filename().string() < files.back().filename().string());
}

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
                    LOG_INFO("c{}-{:05d}-padpadpadpadpadpadpadpad", t, i);
            });
    }
    for (auto &th : pool)
        th.join();
    mylog::FileManager::getInstance().flushFile();

    const std::vector<std::filesystem::path> files = collectLogs("croll");
    CHECK(!files.empty());
    // 保留策略只在滚动时生效，文件数不应超过上限（多进程场景不在测试范围）
    CHECK(files.size() <= kMaxFiles);

    // 关键性质：滚动只能落在行边界上，任何一行都不得被截断或与别的行粘连
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
                allWellFormed = false; // 文件末尾残缺行 = 行被撕裂
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

void testPerThreadFileIntegrity(size_t threadBufferCapacity, const char *basename,
                                const char *label)
{
    section(label);
    constexpr unsigned kThreads = 4;
    constexpr size_t kPerThread = 5000;

    setFileConfig(basename, threadBufferCapacity, 1ULL << 30, 0); // 容量足够大，测不滚动
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
        th.join(); // TlsOwn 析构时把各自剩余数据写进自己的文件

    const std::vector<std::filesystem::path> files = collectPerThreadLogs(basename);
    CHECK_EQ(files.size(), static_cast<size_t>(kThreads)); // 每线程恰好一个文件

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

    // 每个线程的记录必须完整落在它自己的那一个文件里（互相不混写）
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

void testPerThreadFileRolling()
{
    section("per-thread files (A2) rolling + maxFiles");
    constexpr size_t kFileCapacity = 8192;
    constexpr size_t kMaxFiles = 2;
    constexpr size_t kMessages = 3000;

    setFileConfig("ptr", 0, kFileCapacity, kMaxFiles);
    useFileOutputPerThread();

    std::thread worker(
        []
        {
            for (size_t i = 0; i < kMessages; ++i)
                LOG_INFO("ptr-{:06d}-padding-padding-padding", i);
        });
    worker.join();

    const std::vector<std::filesystem::path> files = collectPerThreadLogs("ptr");
    CHECK(!files.empty());
    CHECK(files.size() > 1);        // 确实发生了滚动
    CHECK(files.size() <= kMaxFiles); // maxFiles 是每线程独立的上限

    // 关键性质：滚动只能落在行边界上，任何一行都不得被截断
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
    // 保留策略丢弃最旧的文件，因此行数少于写入量是预期的
    CHECK(lineCount <= kMessages);
}

void testEscapeToggle()
{
    section("escapeControlChars toggle");

    // 默认必须开启转义
    useCaptureOutput();
    LOG_INFO("a\nb");
    const std::string escaped = captured();
    CHECK(std::count(escaped.begin(), escaped.end(), '\n') == 1);
    CHECK(escaped.find("a\\nb") != std::string::npos);

    // 显式关闭后不再转义（唯一的“不注入保护”开关）
    mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
    cfg.escapeControlChars = false;
    mylog::Logger::getInstance().setConfig(std::move(cfg));
    clearCaptured();
    LOG_INFO("a\nb");
    const std::string raw = captured();
    CHECK(std::count(raw.begin(), raw.end(), '\n') == 2);
    CHECK(raw.find("a\nb") != std::string::npos);

    // timestampWithMs = false：秒级截断，仍必须是完整的一行
    cfg = mylog::Logger::getInstance().getConfig();
    cfg.escapeControlChars = true;
    cfg.timestampWithMs = false;
    mylog::Logger::getInstance().setConfig(std::move(cfg));
    clearCaptured();
    LOG_INFO("no-ms");
    const std::string out = captured();
    CHECK(out.size() > 20);
    CHECK_EQ(out[19], ' '); // "YYYY-MM-DD HH:MM:SS " 后直接接级别
    CHECK_EQ(out.back(), '\n');
    CHECK(messageOf(out) == "no-ms");

    cfg = mylog::Logger::getInstance().getConfig();
    cfg.timestampWithMs = true;
    mylog::Logger::getInstance().setConfig(std::move(cfg));
    useCaptureOutput();
}

void testBasenameValidation()
{
    section("basename validation");
    using mylog::FileManager;
    CHECK(FileManager::isValidBasename("app"));
    CHECK(FileManager::isValidBasename("app.log-1_2"));
    CHECK(!FileManager::isValidBasename(""));
    CHECK(!FileManager::isValidBasename("."));
    CHECK(!FileManager::isValidBasename(".."));
    CHECK(!FileManager::isValidBasename("../evil"));
    CHECK(!FileManager::isValidBasename("a/b"));
    CHECK(!FileManager::isValidBasename("/tmp/x"));
    CHECK(!FileManager::isValidBasename("a b"));
    CHECK(!FileManager::isValidBasename(std::string(129, 'a')));
}

void writeFile(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void testLuaConfig()
{
    section("lua config");
    const std::filesystem::path good = "config_good.lua";
    const mylog::Logger::Config savedCfg = mylog::Logger::getInstance().getConfig();
    writeFile(good,
              "logger = {\n"
              "  suppressLevel = 3,\n"
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
    CHECK_EQ(static_cast<int>(mylog::Logger::getInstance().getSuppressLevel()), 3);
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

    // 数值越界：钳制而非拒绝（maxLogLength 钳到下限）
    const std::filesystem::path clampCfg = "config_clamp.lua";
    writeFile(clampCfg,
              "logger = {\n"
              "  maxLogLength = 1\n"
              "}\n");
    CHECK(mylog::reloadConfig(clampCfg));
    const mylog::Logger::Config clamped = mylog::Logger::getInstance().getConfig();
    CHECK_EQ(clamped.maxLogLength, mylog::detail::kMinMaxLogLength);
    // 恢复测试前的配置，避免影响后续用例
    mylog::Logger::getInstance().setConfig(savedCfg);

    // 非法 basename：整体拒绝，旧配置保持生效
    const std::filesystem::path bad = "config_bad.lua";
    writeFile(bad, "fileManager = { basename = \"../evil\" }\n");
    CHECK(!mylog::reloadConfig(bad));
    CHECK_EQ(mylog::FileManager::getInstance().getConfig().basename, std::string("luaapp"));

    // 越界 suppressLevel：整体拒绝，抑制级别保持原值不变。
    // 抑制级别是 Logger 的独立原子，不随 setLoggerConfig(savedCfg) 回退，
    // 因此这里仍是上面 config_good.lua 写入的 3。
    const std::filesystem::path badLevel = "config_bad_level.lua";
    writeFile(badLevel, "logger = { suppressLevel = 99 }\n");
    CHECK(!mylog::reloadConfig(badLevel));
    CHECK_EQ(static_cast<int>(mylog::Logger::getInstance().getSuppressLevel()), 3);

    // 语法错误
    const std::filesystem::path broken = "config_broken.lua";
    writeFile(broken, "logger = { suppressLevel = \n");
    CHECK(!mylog::reloadConfig(broken));

    // 安全断言：配置脚本不能执行命令（未调用 luaL_openlibs，os 不可用）
    const std::filesystem::path evil = "config_evil.lua";
    const std::string marker = "/tmp/mylog_pwned_marker";
    std::error_code ec;
    std::filesystem::remove(marker, ec);
    writeFile(evil, "os.execute(\"touch " + marker + "\")\n");
    CHECK(!mylog::reloadConfig(evil));
    CHECK(!std::filesystem::exists(marker));

    // 抑制级别随整体配置一并恢复，确保后续用例不受本用例影响
    mylog::Logger::getInstance().setConfig(savedCfg);

    std::filesystem::remove(good, ec);
    std::filesystem::remove(bad, ec);
    std::filesystem::remove(badLevel, ec);
    std::filesystem::remove(clampCfg, ec);
    std::filesystem::remove(broken, ec);
    std::filesystem::remove(evil, ec);
}

void testFatalAction()
{
    section("FATAL action");
    // NONE：不终止进程
    useCaptureOutput();
    LOG_FATAL("fatal-none");
    CHECK(captured().find("fatal-none") != std::string::npos);

    // ABORT：子进程应被 SIGABRT 终止
    struct rlimit rl
    {
    };
    rl.rlim_cur = 0;
    rl.rlim_max = 0;
    ::setrlimit(RLIMIT_CORE, &rl); // 不产生 core 文件污染工作目录

    const pid_t pid = ::fork();
    if (pid == 0)
    {
        // 仅在子进程里切到 ABORT，避免影响父进程
        mylog::Logger::Config abortCfg = mylog::Logger::getInstance().getConfig();
        abortCfg.fatalAction = mylog::FATAL_ACTION::ABORT;
        mylog::Logger::getInstance().setConfig(std::move(abortCfg));
        LOG_FATAL("fatal-abort");
        ::_exit(0); // 不应到达
    }
    CHECK(pid > 0);
    int status = 0;
    ::waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status));
    CHECK_EQ(WTERMSIG(status), SIGABRT);

    useCaptureOutput();
}

} // namespace

int main()
{
    // 所有文件都在临时目录里产生
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
    testPerThreadFileIntegrity(0, "ptf_nobuf", "per-thread files (A2), unbuffered");
    testPerThreadFileIntegrity(65536, "ptf_tls", "per-thread files (A2), thread buffer");
    testPerThreadFileRolling();
    testEscapeToggle();
    testBasenameValidation();
    testLuaConfig();
    testFatalAction();

    std::fprintf(stderr, "\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
