#pragma once

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
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace mylog_test
{
    extern std::atomic<int> g_checks;
    extern std::atomic<int> g_failures;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++::mylog_test::g_checks;                                                                                      \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++::mylog_test::g_failures;                                                                                \
            std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                     \
        }                                                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        ++::mylog_test::g_checks;                                                                                      \
        if (!((a) == (b)))                                                                                             \
        {                                                                                                              \
            ++::mylog_test::g_failures;                                                                                \
            std::fprintf(stderr, "  FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b);                              \
        }                                                                                                              \
    } while (0)

    void section(const char *name);

    void captureOutput(void *ctx, std::string_view msg);
    void noFlush(void *ctx);
    std::string captured();
    void clearCaptured();

    void useCaptureOutput();
    void useFileOutput();
    void useFileOutputPerThread();
    void setFileConfig(size_t threadBuffer, size_t fileCapacity, size_t maxFiles);

    void clearTestLogs();
    std::vector<std::filesystem::path> collectLogs();
    std::vector<std::filesystem::path> collectPerThreadLogs();
    std::string readText(const std::filesystem::path &path);
    std::vector<std::string> readLinesFrom(const std::vector<std::filesystem::path> &files);
    std::vector<std::string> readAllLines();
    bool lineWellFormed(const std::string &line);
    std::string messageOf(const std::string &line);
    void writeTestFile(const std::filesystem::path &path, const std::string &text);

    // 测试用例声明
    void testEscape();
    void testSuppress();
    void testMessageLengthCap();
    void testLoggerTunables();
    void testReentrancy();
    void testEscapeToggle();
    void testFatalAction();

    void testMultiThreadIntegrity(size_t threadBufferCapacity, const char *label);
    void testThreadBufferDrain();
    void testRollingAndRetention();
    void testConcurrentRolling();
    void testPerThreadFileIntegrity(size_t threadBufferCapacity, const char *label);
    void testPerThreadFileRolling();
    void testUnifiedFilenameAndNanoseconds();

    void testLuaConfig();

    void testReflection();
    void testMacroFreeLogging();
} // namespace mylog_test
