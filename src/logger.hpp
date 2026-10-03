#pragma once

#include "configHolder.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <unistd.h>

#ifndef MYLOG_ACTIVE_LEVEL
#define MYLOG_ACTIVE_LEVEL 0
#endif

namespace mylog
{

    enum class LEVEL : uint8_t
    {
        TRACE = 0,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL
    };

    inline constexpr size_t kLevelCount = 6;

    enum class FATAL_ACTION : uint8_t
    {
        ABORT = 0,
        EXIT,
        NONE
    };

    struct OutputFunction
    {
        using Fn = void (*)(void *ctx, std::string_view msg);

        Fn fn = nullptr;
        void *ctx = nullptr;

        constexpr OutputFunction() noexcept = default;
        constexpr OutputFunction(Fn f, void *c = nullptr) noexcept : fn(f), ctx(c) {}

        void operator()(std::string_view msg) const
        {
            if (fn != nullptr)
                fn(ctx, msg);
        }

        explicit constexpr operator bool() const noexcept { return fn != nullptr; }
    };

    struct FlushFunction
    {
        using Fn = void (*)(void *ctx);

        Fn fn = nullptr;
        void *ctx = nullptr;

        constexpr FlushFunction() noexcept = default;
        constexpr FlushFunction(Fn f, void *c = nullptr) noexcept : fn(f), ctx(c) {}

        void operator()() const
        {
            if (fn != nullptr)
                fn(ctx);
        }

        explicit constexpr operator bool() const noexcept { return fn != nullptr; }
    };

    namespace detail
    {
        inline void stdoutOutput(void * /*ctx*/, std::string_view msg)
        {
            if (!msg.empty())
                std::fwrite(msg.data(), 1, msg.size(), stdout);
        }

        inline void stdoutFlush(void * /*ctx*/)
        {
            std::fflush(stdout);
        }

        inline const char *levelToStr(const std::array<const char *, kLevelCount> *lv2str,
                                      LEVEL level) noexcept
        {
            const int idx = static_cast<int>(level);
            if (idx < static_cast<int>(LEVEL::TRACE) || idx >= static_cast<int>(kLevelCount))
                return "UNKNOWN";
            return (*lv2str)[idx];
        }

        inline constexpr size_t kMinMaxLogLength = 1u << 6;
        inline constexpr size_t kMaxMaxLogLength = 1u << 26;

        inline constexpr int kActiveLevel = MYLOG_ACTIVE_LEVEL;

        template <typename T>
        constexpr T clampValue(T value, T lo, T hi) noexcept
        {
            return value < lo ? lo : (value > hi ? hi : value);
        }

        inline void putDigits(char *dst, int value, int width) noexcept
        {
            for (int i = width - 1; i >= 0; --i)
            {
                dst[i] = static_cast<char>('0' + value % 10);
                value /= 10;
            }
        }

        inline void appendTimestamp(std::string &out, bool withMs, bool utc)
        {
            using namespace std::chrono;
            const auto now = system_clock::now();
            const auto secs = floor<seconds>(now);
            const std::time_t s = system_clock::to_time_t(secs);

            static thread_local struct TimestampCache
            {
                std::time_t second = static_cast<std::time_t>(-1);
                char text[20] = "0000-00-00 00:00:00";
            } cache;
            if (s != cache.second)
            {
                std::tm tm{};
                if (utc)
                    gmtime_r(&s, &tm);
                else
                    localtime_r(&s, &tm);
                char *p = cache.text;
                putDigits(p + 0, clampValue(tm.tm_year + 1900, 0, 9999), 4);
                p[4] = '-';
                putDigits(p + 5, clampValue(tm.tm_mon + 1, 0, 99), 2);
                p[7] = '-';
                putDigits(p + 8, clampValue(tm.tm_mday, 0, 99), 2);
                p[10] = ' ';
                putDigits(p + 11, clampValue(tm.tm_hour, 0, 99), 2);
                p[13] = ':';
                putDigits(p + 14, clampValue(tm.tm_min, 0, 99), 2);
                p[16] = ':';
                putDigits(p + 17, clampValue(tm.tm_sec, 0, 99), 2);
                p[19] = '\0';
                cache.second = s;
            }
            out.append(cache.text, 19);

            if (withMs)
            {
                const long long ms = duration_cast<milliseconds>(now - secs).count();
                const char buf[4] = {'.', static_cast<char>('0' + ms / 100),
                                     static_cast<char>('0' + (ms / 10) % 10),
                                     static_cast<char>('0' + ms % 10)};
                out.append(buf, 4);
            }
        }

        template <typename Int>
        inline void appendInt(std::string &out, Int value)
        {
            char buf[24];
            const auto res = std::to_chars(buf, buf + sizeof(buf), value);
            out.append(buf, static_cast<size_t>(res.ptr - buf));
        }

        inline long long threadId() noexcept
        {
            static thread_local const long long tid = static_cast<long long>(::gettid());
            return tid;
        }

        inline bool needsEscape(const char *data, size_t size) noexcept
        {
            for (size_t i = 0; i < size; ++i)
            {
                const unsigned char c = static_cast<unsigned char>(data[i]);
                if (c < 0x20 || c == 0x7f)
                    return true;
            }
            return false;
        }

        inline void appendEscaped(std::string &out, std::string_view sv)
        {
            if (sv.empty())
                return;
            if (!needsEscape(sv.data(), sv.size()))
            {
                out.append(sv.data(), sv.size());
                return;
            }
            for (char c : sv)
            {
                switch (c)
                {
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\033':
                    out += "\\e";
                    break;
                default:
                {
                    const unsigned char uc = static_cast<unsigned char>(c);
                    if (uc < 0x20 || uc == 0x7f)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\x%02x", uc);
                        out.append(buf, 4);
                    }
                    else
                    {
                        out.push_back(c);
                    }
                }
                }
            }
        }

    } // namespace detail

    inline constexpr std::array<const char *, kLevelCount> LV2STR = {
        " TRACE ", " DEBUG ", " INFO ", " WARN ", " ERROR ", " FATAL "};
    inline constexpr std::array<const char *, kLevelCount> LV2STR_VIVID = {
        "\033[7;37m TRACE \033[0m", "\033[7;36m DEBUG \033[0m", "\033[7;32m INFO \033[0m",
        "\033[7;33m WARN \033[0m", "\033[7;31m ERROR \033[0m", "\033[5;41m FATAL \033[0m"};

    class Logger
    {
    public:
        struct Config
        {
            const std::array<const char *, kLevelCount> *lv2str = &LV2STR;
            FATAL_ACTION fatalAction = FATAL_ACTION::ABORT;
            bool escapeControlChars = true;
            bool timestampWithMs = true;
            bool timestampUtc = false;
            size_t maxLogLength = 1u << 20;
            OutputFunction outputFunction{&detail::stdoutOutput, nullptr};
            FlushFunction flushFunction{&detail::stdoutFlush, nullptr};

            void normalize() noexcept
            {
                maxLogLength = std::min(std::max(maxLogLength, detail::kMinMaxLogLength),
                                        detail::kMaxMaxLogLength);
                if (lv2str == nullptr)
                    lv2str = &LV2STR;
            }
        };

        Config getConfig() const noexcept
        {
            return refConfig();
        }

        void setConfig(Config cfg)
        {
            cfg.normalize();
            cfgHolder_.publish(std::move(cfg));
        }
        
        inline static Logger &getInstance() noexcept
        {
            static Logger *instance = new Logger();
            return *instance;
        }

        void setOutputFunction(OutputFunction fn)
        {
            Config cfg = getConfig();
            cfg.outputFunction = fn;
            setConfig(std::move(cfg));
        }

        void setFlushFunction(FlushFunction fn)
        {
            Config cfg = getConfig();
            cfg.flushFunction = fn;
            setConfig(std::move(cfg));
        }

        LEVEL getSuppressLevel() const noexcept
        {
            return static_cast<LEVEL>(suppressLevel_.load(std::memory_order_relaxed));
        }

        void setSuppressLevel(LEVEL level) noexcept
        {
            suppressLevel_.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
        }

        inline void log(LEVEL level, const char *file, int line, const char *func,
                        std::string_view message)
        {
            thread_local bool inLog = false;
            if (inLog)
                return;
            struct ReentrancyGuard
            {
                bool &flag;
                explicit ReentrancyGuard(bool &f) : flag(f) { flag = true; }
                ~ReentrancyGuard() { flag = false; }
            } guard{inLog};

            const Config &cfg = refConfig();

            if (message.size() > cfg.maxLogLength)
                message = message.substr(0, cfg.maxLogLength);

            thread_local std::string buf;
            buf.clear();
            buf.reserve(256);

            detail::appendTimestamp(buf, cfg.timestampWithMs, cfg.timestampUtc);
            buf.append(detail::levelToStr(cfg.lv2str, level));
            buf.push_back('[');
            detail::appendInt(buf, detail::threadId());
            buf.push_back(']');
            buf.append(func);
            buf.append(">>");
            if (cfg.escapeControlChars)
                detail::appendEscaped(buf, message);
            else if (!message.empty())
                buf.append(message.data(), message.size());
            buf.append(" (");
            buf.append(file);
            buf.push_back(':');
            detail::appendInt(buf, line);
            buf.push_back(')');
            buf.push_back('\n');

            cfg.outputFunction(buf);

            if (level == LEVEL::FATAL)
            {
                cfg.flushFunction();
                std::fflush(nullptr);
                switch (cfg.fatalAction)
                {
                case FATAL_ACTION::ABORT:
                    std::abort();
                case FATAL_ACTION::EXIT:
                    std::exit(EXIT_FAILURE);
                case FATAL_ACTION::NONE:
                    break;
                }
            }
        }

    private:
        ConfigHolder<Config> cfgHolder_;
        std::atomic<uint8_t> suppressLevel_{static_cast<uint8_t>(LEVEL::TRACE)};

        const Config &refConfig() const noexcept
        {
            thread_local ConfigSnapshot<Config> snapshot(cfgHolder_);
            return snapshot.refConfig();
        }

        Logger() = default;
        ~Logger() = default;
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
    };

    inline LEVEL currentSuppressLevel() noexcept
    {
        return Logger::getInstance().getSuppressLevel();
    }

    template <typename... Args>
    inline void logger(LEVEL level, const char *file, int line, const char *func,
                       std::format_string<Args...> fmt, Args &&...args)
    {
        thread_local std::string message;
        message.clear();
        message.reserve(256);
        std::format_to(std::back_inserter(message), fmt, std::forward<Args>(args)...);
        Logger::getInstance().log(level, file, line, func, message);
    }

    inline void logger(LEVEL level, const char *file, int line, const char *func,
                       std::string_view fmt)
    {
        Logger::getInstance().log(level, file, line, func, fmt);
    }

    inline void logger(LEVEL level, const char *file, int line, const char *func)
    {
        Logger::getInstance().log(level, file, line, func, {});
    }

} // namespace mylog

#define MYLOG_LOG_IMPL(lv, ...)                                                              \
    do                                                                                       \
    {                                                                                        \
        if constexpr (static_cast<int>(::mylog::LEVEL::lv) >= ::mylog::detail::kActiveLevel) \
        {                                                                                    \
            if (::mylog::LEVEL::lv >= ::mylog::currentSuppressLevel())                       \
            {                                                                                \
                ::mylog::logger(::mylog::LEVEL::lv, __FILE__, __LINE__,                      \
                                __func__ __VA_OPT__(, ) __VA_ARGS__);                        \
            }                                                                                \
        }                                                                                    \
    } while (0)

#define LOG_TRACE(...) MYLOG_LOG_IMPL(TRACE, __VA_ARGS__)
#define LOG_DEBUG(...) MYLOG_LOG_IMPL(DEBUG, __VA_ARGS__)
#define LOG_INFO(...) MYLOG_LOG_IMPL(INFO, __VA_ARGS__)
#define LOG_WARN(...) MYLOG_LOG_IMPL(WARN, __VA_ARGS__)

#define LOG_ERROR(...)                                                                          \
    do                                                                                          \
    {                                                                                           \
        if constexpr (static_cast<int>(::mylog::LEVEL::ERROR) >= ::mylog::detail::kActiveLevel) \
        {                                                                                       \
            ::mylog::logger(::mylog::LEVEL::ERROR, __FILE__, __LINE__,                          \
                            __func__ __VA_OPT__(, ) __VA_ARGS__);                               \
        }                                                                                       \
    } while (0)

#define LOG_FATAL(...)                                                                          \
    do                                                                                          \
    {                                                                                           \
        if constexpr (static_cast<int>(::mylog::LEVEL::FATAL) >= ::mylog::detail::kActiveLevel) \
        {                                                                                       \
            ::mylog::logger(::mylog::LEVEL::FATAL, __FILE__, __LINE__,                          \
                            __func__ __VA_OPT__(, ) __VA_ARGS__);                               \
        }                                                                                       \
    } while (0)
