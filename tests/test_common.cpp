#include "test_common.hpp"

namespace mylog_test
{
    std::atomic<int> g_checks{0};
    std::atomic<int> g_failures{0};

    void section(const char *name)
    {
        std::fprintf(stderr, "\n[%s]\n", name);
    }

    static std::mutex g_captureMtx;
    static std::string g_capture;

    void captureOutput(void * /*ctx*/, std::string_view msg)
    {
        std::lock_guard<std::mutex> lock(g_captureMtx);
        g_capture.append(msg.data(), msg.size());
    }

    void noFlush(void * /*ctx*/) {}

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

    void useFileOutput()
    {
        mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
        cfg.outputFunction = mylog::FileManager::createFileOutputFunction();
        cfg.flushFunction = mylog::FileManager::createFileFlushFunction();
        cfg.fatalAction = mylog::FATAL_ACTION::NONE;
        mylog::Logger::getInstance().setConfig(cfg);
    }

    void useFileOutputPerThread()
    {
        mylog::Logger::Config cfg = mylog::Logger::getInstance().getConfig();
        cfg.outputFunction = mylog::FileManager::createPerThreadFileOutputFunction();
        cfg.flushFunction = mylog::FileManager::createPerThreadFileFlushFunction();
        cfg.fatalAction = mylog::FATAL_ACTION::NONE;
        mylog::Logger::getInstance().setConfig(cfg);
    }

    void setFileConfig(size_t threadBuffer, size_t fileCapacity, size_t maxFiles)
    {
        mylog::FileManager::Config fcfg = mylog::FileManager::getInstance().getConfig();
        fcfg.threadBufferCapacity = threadBuffer;
        fcfg.fileCapacity = fileCapacity;
        fcfg.maxFiles = maxFiles;
        mylog::FileManager::getInstance().setConfig(fcfg);
    }

    void clearTestLogs()
    {
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(".", ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".log")
                std::filesystem::remove(entry.path(), ec);
        }
    }

    std::vector<std::filesystem::path> collectLogs()
    {
        std::vector<std::filesystem::path> out;
        const long long tid = mylog::FileManager::getInstance().getSingleFileTid();
        const std::string suffix = mylog::detail::getFileSuffix(tid);
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(".", ec))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string name = entry.path().filename().string();
            if (mylog::detail::isValidLogFilename(name, suffix))
                out.push_back(entry.path());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<std::filesystem::path> collectPerThreadLogs()
    {
        std::vector<std::filesystem::path> out;
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(".", ec))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string name = entry.path().filename().string();
            if (name.size() > 30 && name.ends_with(".log"))
            {
                const size_t lastUnder = name.rfind('_');
                if (lastUnder != std::string::npos && lastUnder >= 25)
                {
                    const std::string suffix = name.substr(lastUnder);
                    if (mylog::detail::isValidLogFilename(name, suffix))
                        out.push_back(entry.path());
                }
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::string readText(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

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

    std::vector<std::string> readAllLines()
    {
        return readLinesFrom(collectLogs());
    }

    bool lineWellFormed(const std::string &line)
    {
        if (line.size() < 31 || line.back() != '\n')
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
        if (line[19] != '.' || line[29] != ' ')
            return false;
        for (int i = 20; i < 29; ++i)
        {
            const char c = line[static_cast<size_t>(i)];
            if (c < '0' || c > '9')
                return false;
        }
        return true;
    }

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

    void writeTestFile(const std::filesystem::path &path, const std::string &text)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
} // namespace mylog_test
