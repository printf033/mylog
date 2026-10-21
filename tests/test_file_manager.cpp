#include "test_common.hpp"

namespace mylog_test
{
    void testMultiThreadIntegrity(size_t threadBufferCapacity, const char *label)
    {
        section(label);
        constexpr unsigned kThreads = 4;
        constexpr size_t kPerThread = 5000;

        mylog::FileManager::getInstance().rotateFile();
        clearTestLogs();
        setFileConfig(threadBufferCapacity, 1ULL << 30, 0);
        mylog::FileManager::getInstance().rotateFile();
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

        const std::vector<std::string> lines = readAllLines();
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
        mylog::FileManager::getInstance().rotateFile();
        clearTestLogs();
        setFileConfig(1ULL << 20, 1ULL << 30, 0);
        mylog::FileManager::getInstance().rotateFile();
        useFileOutput();
        mylog::FileManager::getInstance().flushFile();

        std::thread worker(
            []
            {
                for (int i = 0; i < 50; ++i)
                    LOG_INFO("drain-{}", i);
            });
        worker.join();

        const std::vector<std::string> stillBuffered = readAllLines();
        mylog::FileManager::getInstance().flushFile();

        const std::vector<std::string> lines = readAllLines();
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

        mylog::FileManager::getInstance().rotateFile();
        clearTestLogs();
        setFileConfig(kThreadBuffer, kFileCapacity, kMaxFiles);
        mylog::FileManager::getInstance().rotateFile();
        useFileOutput();
        mylog::FileManager::getInstance().flushFile();

        for (int i = 0; i < 3000; ++i)
            mylog::info("roll-message-{:06d}-padding-padding-padding", i);
        mylog::FileManager::getInstance().flushFile();

        const std::vector<std::filesystem::path> files = collectLogs();
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

    void testConcurrentRolling()
    {
        section("concurrent rolling (line integrity)");
        constexpr unsigned kThreads = 4;
        constexpr size_t kPerThread = 4000;
        constexpr size_t kThreadBuffer = 4096;
        constexpr size_t kFileCapacity = 16384;
        constexpr size_t kMaxFiles = 4;

        mylog::FileManager::getInstance().rotateFile();
        clearTestLogs();
        setFileConfig(kThreadBuffer, kFileCapacity, kMaxFiles);
        mylog::FileManager::getInstance().rotateFile();
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

        const std::vector<std::filesystem::path> files = collectLogs();
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

    void testPerThreadFileIntegrity(size_t threadBufferCapacity, const char *label)
    {
        section(label);
        constexpr unsigned kThreads = 4;
        constexpr size_t kPerThread = 5000;

        clearTestLogs();
        setFileConfig(threadBufferCapacity, 1ULL << 30, 0);
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

        const std::vector<std::filesystem::path> files = collectPerThreadLogs();
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

    void testPerThreadFileRolling()
    {
        section("per-thread files rolling + maxFiles");
        constexpr size_t kFileCapacity = 8192;
        constexpr size_t kMaxFiles = 2;
        constexpr size_t kMessages = 3000;

        clearTestLogs();
        setFileConfig(0, kFileCapacity, kMaxFiles);
        useFileOutputPerThread();

        std::thread worker(
            []
            {
                for (size_t i = 0; i < kMessages; ++i)
                    mylog::info("ptr-{:06d}-padding-padding-padding", i);
            });
        worker.join();

        const std::vector<std::filesystem::path> files = collectPerThreadLogs();
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

    void testUnifiedFilenameAndNanoseconds()
    {
        section("unified filename format & nanoseconds precision");

        // 1. 验证纳秒级时间格式化（统一 29 字符）
        std::string tsStr;
        mylog::detail::appendTimestamp(tsStr, false);
        CHECK_EQ(tsStr.size(), 29u);
        CHECK(tsStr[19] == '.');
        for (size_t i = 20; i < 29; ++i)
            CHECK(tsStr[i] >= '0' && tsStr[i] <= '9');

        // 2. 验证文件名生成函数 getFilename
        const long long dummyTid = 88888;
        const std::string fnUnified = mylog::detail::getFilename(dummyTid);
        CHECK(fnUnified.ends_with("_88888.log"));
        CHECK(mylog::detail::isValidLogFilename(fnUnified, "_88888.log"));

        // 3. 验证统一后缀生成
        CHECK_EQ(mylog::detail::getFileSuffix(dummyTid), std::string("_88888.log"));

        // 4. 验证防误删漏洞机制：非本库规则命名的同后缀文件不会被判定为有效日志文件
        CHECK(!mylog::detail::isValidLogFilename("evil_88888.log", "_88888.log"));
        CHECK(!mylog::detail::isValidLogFilename("20261021-120000-000000000_88888.log", "_88888.log"));
        CHECK(!mylog::detail::isValidLogFilename("20261021_120000_00000000_88888.log", "_88888.log"));

        // 5. 验证实际落盘文件名：共享单文件模式
        mylog::FileManager::getInstance().rotateFile();
        clearTestLogs();
        setFileConfig(4096, 1ULL << 30, 0);
        mylog::FileManager::getInstance().rotateFile();
        useFileOutput();
        mylog::info("verify-single-mode");
        mylog::FileManager::getInstance().flushFile();

        const auto singleFiles = collectLogs();
        CHECK_EQ(singleFiles.size(), 1u);
        const std::string actualSingleName = singleFiles.front().filename().string();
        CHECK(actualSingleName.ends_with(std::format("_{}.log", mylog::FileManager::getInstance().getSingleFileTid())));
        CHECK(mylog::detail::isValidLogFilename(
            actualSingleName, std::format("_{}.log", mylog::FileManager::getInstance().getSingleFileTid())));

        // 6. 验证实际落盘文件名：每线程独立文件模式
        clearTestLogs();
        setFileConfig(4096, 1ULL << 30, 0);
        useFileOutputPerThread();

        std::thread worker([] { mylog::info("verify-per-thread-worker"); });
        worker.join();

        const auto multiFiles = collectPerThreadLogs();
        CHECK_EQ(multiFiles.size(), 1u);
        const std::string actualMultiName = multiFiles.front().filename().string();
        CHECK(mylog::detail::isValidLogFilename(actualMultiName, actualMultiName.substr(actualMultiName.rfind('_'))));
    }
} // namespace mylog_test
