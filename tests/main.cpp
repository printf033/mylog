#include "test_common.hpp"

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

    // 1. Logger 核心功能测试
    mylog_test::testEscape();
    mylog_test::testSuppress();
    mylog_test::testMessageLengthCap();
    mylog_test::testLoggerTunables();
    mylog_test::testReentrancy();
    mylog_test::testEscapeToggle();
    mylog_test::testFatalAction();

    // 2. FileManager 文件落盘与并发测试
    mylog_test::testMultiThreadIntegrity(65536, "multithread + thread buffer");
    mylog_test::testMultiThreadIntegrity(0, "multithread + shared buffer (thread buffer off)");
    mylog_test::testThreadBufferDrain();
    mylog_test::testRollingAndRetention();
    mylog_test::testConcurrentRolling();
    mylog_test::testPerThreadFileIntegrity(0, "per-thread files, unbuffered");
    mylog_test::testPerThreadFileIntegrity(65536, "per-thread files, thread buffer");
    mylog_test::testPerThreadFileRolling();
    mylog_test::testUnifiedFilenameAndNanoseconds();

    // 3. Lua 配置加载与沙箱测试
    mylog_test::testLuaConfig();

    // 4. 反射与无宏 API 测试
    mylog_test::testReflection();
    mylog_test::testMacroFreeLogging();

    std::fprintf(stderr, "\n%d checks, %d failures\n", ::mylog_test::g_checks.load(), ::mylog_test::g_failures.load());
    return ::mylog_test::g_failures.load() == 0 ? 0 : 1;
}
