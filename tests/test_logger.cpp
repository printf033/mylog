#include "test_common.hpp"

namespace mylog_test
{
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

        // M-3：宏路径与函数 API 抑制语义一致
        clearCaptured();
        LOG_INFO("macro-suppressed");
        CHECK(captured().empty());
        LOG_WARN("macro-passes");
        CHECK(captured().find("macro-passes") != std::string::npos);

        mylog::setSuppressLevel(mylog::LEVEL::TRACE);
    }

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

        // 统一纳秒级格式化验证（29 字符）
        std::string utcStamp, localStamp;
        mylog::detail::appendTimestamp(utcStamp, true);
        mylog::detail::appendTimestamp(localStamp, false);
        CHECK_EQ(utcStamp.size(), 29u);
        CHECK_EQ(localStamp.size(), 29u);

        cfg = mylog::Logger::getInstance().getConfig();
        cfg.maxLogLength = 1ULL << 20;
        mylog::Logger::getInstance().setConfig(cfg);
    }

    static std::atomic<int> g_reentrantCalls{0};

    static void reentrantOutput(void * /*ctx*/, std::string_view msg)
    {
        g_reentrantCalls.fetch_add(1);
        captureOutput(nullptr, msg);
        mylog::info("from-inside-callback");
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

        mylog::info("outer");
        CHECK_EQ(g_reentrantCalls.load(), 1);
        CHECK(captured().find("outer") != std::string::npos);
        CHECK(captured().find("from-inside-callback") == std::string::npos);

        useCaptureOutput();
    }

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
} // namespace mylog_test
