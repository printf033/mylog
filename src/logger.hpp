#pragma once

#include "configHolder.hpp"
#include "reflection.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <iterator>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <unistd.h>
#include <utility>

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

    enum class FATAL_ACTION : uint8_t
    {
        NONE = 0,
        EXIT,
        ABORT
    };

#if MYLOG_HAS_REFLECTION
    inline constexpr size_t kLevelCount = reflect::countEnumMembers<LEVEL>();
#else
    inline constexpr size_t kLevelCount = 6;
#endif
    static_assert(kLevelCount == 6, "Expected 6 log levels");

    /**
     * @brief 将日志级别枚举转换为对应的字面量字符串
     *
     * @param level 日志级别
     * @return constexpr std::string_view 对应的字符串名称（如 "INFO", "WARN"）
     */
    inline constexpr std::string_view levelToString(LEVEL level) noexcept
    {
#if MYLOG_HAS_REFLECTION
        return reflect::enumToString(level);
#else
        switch (level)
        {
        case LEVEL::TRACE:
            return "TRACE";
        case LEVEL::DEBUG:
            return "DEBUG";
        case LEVEL::INFO:
            return "INFO";
        case LEVEL::WARN:
            return "WARN";
        case LEVEL::ERROR:
            return "ERROR";
        case LEVEL::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
#endif
    }

    /**
     * @brief 从字符串反向解析日志级别枚举
     *
     * @param str 日志级别字符串
     * @param caseInsensitive 是否忽略大小写（默认为 true）
     * @return constexpr std::optional<LEVEL> 解析成功的级别枚举或 std::nullopt
     */
    inline constexpr std::optional<LEVEL> parseLevel(std::string_view str, bool caseInsensitive = true) noexcept
    {
#if MYLOG_HAS_REFLECTION
        return reflect::parseEnum<LEVEL>(str, caseInsensitive);
#else
        constexpr std::pair<LEVEL, std::string_view> entries[] = {{LEVEL::TRACE, "TRACE"}, {LEVEL::DEBUG, "DEBUG"},
                                                                  {LEVEL::INFO, "INFO"},   {LEVEL::WARN, "WARN"},
                                                                  {LEVEL::ERROR, "ERROR"}, {LEVEL::FATAL, "FATAL"}};

        for (const auto &[v, name] : entries)
        {
            if (caseInsensitive)
            {
                if (name.size() == str.size())
                {
                    bool match = true;
                    for (size_t i = 0; i < name.size(); ++i)
                    {
                        char c1 = name[i];
                        char c2 = str[i];
                        if (c1 >= 'a' && c1 <= 'z')
                            c1 = static_cast<char>(c1 - 'a' + 'A');
                        if (c2 >= 'a' && c2 <= 'z')
                            c2 = static_cast<char>(c2 - 'a' + 'A');
                        if (c1 != c2)
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match)
                        return v;
                }
            }
            else
            {
                if (name == str)
                    return v;
            }
        }
        return std::nullopt;
#endif
    }

    /**
     * @brief 将致命级别动作枚举转换为对应的字面量字符串
     *
     * @param act 致命级别行为动作枚举
     * @return constexpr std::string_view 对应的字符串名称（如 "NONE", "EXIT", "ABORT"）
     */
    inline constexpr std::string_view fatalActionToString(FATAL_ACTION act) noexcept
    {
#if MYLOG_HAS_REFLECTION
        return reflect::enumToString(act);
#else
        switch (act)
        {
        case FATAL_ACTION::NONE:
            return "NONE";
        case FATAL_ACTION::EXIT:
            return "EXIT";
        case FATAL_ACTION::ABORT:
            return "ABORT";
        default:
            return "UNKNOWN";
        }
#endif
    }

    /**
     * @brief 从字符串反向解析致命级别动作枚举
     *
     * @param str 致命级别动作字符串
     * @param caseInsensitive 是否忽略大小写（默认为 true）
     * @return constexpr std::optional<FATAL_ACTION> 解析成功的动作枚举或 std::nullopt
     */
    inline constexpr std::optional<FATAL_ACTION> parseFatalAction(std::string_view str,
                                                                  bool caseInsensitive = true) noexcept
    {
#if MYLOG_HAS_REFLECTION
        return reflect::parseEnum<FATAL_ACTION>(str, caseInsensitive);
#else
        constexpr std::pair<FATAL_ACTION, std::string_view> entries[] = {
            {FATAL_ACTION::NONE, "NONE"}, {FATAL_ACTION::EXIT, "EXIT"}, {FATAL_ACTION::ABORT, "ABORT"}};

        for (const auto &[v, name] : entries)
        {
            if (caseInsensitive)
            {
                if (name.size() == str.size())
                {
                    bool match = true;
                    for (size_t i = 0; i < name.size(); ++i)
                    {
                        char c1 = name[i];
                        char c2 = str[i];
                        if (c1 >= 'a' && c1 <= 'z')
                            c1 = static_cast<char>(c1 - 'a' + 'A');
                        if (c2 >= 'a' && c2 <= 'z')
                            c2 = static_cast<char>(c2 - 'a' + 'A');
                        if (c1 != c2)
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match)
                        return v;
                }
            }
            else
            {
                if (name == str)
                    return v;
            }
        }
        return std::nullopt;
#endif
    }

    /**
     * @brief 日志输出回调函数封装结构体
     *
     * 支持直接输出（fn）与零拷贝缓冲区直写（acquire/release）双重接口契约。
     */
    struct OutputFunction
    {
        using Fn = void (*)(void *ctx, std::string_view msg);
        using BufferAcquireFn = std::string *(*)(void *ctx) noexcept;
        using BufferReleaseFn = void (*)(void *ctx, size_t written) noexcept;

        Fn fn = nullptr;
        void *ctx = nullptr;
        BufferAcquireFn acquire = nullptr;
        BufferReleaseFn release = nullptr;

        constexpr OutputFunction() noexcept = default;
        constexpr OutputFunction(Fn f, void *c = nullptr, BufferAcquireFn acq = nullptr,
                                 BufferReleaseFn rel = nullptr) noexcept
            : fn(f),
              ctx(c),
              acquire(acq),
              release(rel)
        {
        }

        /**
         * @brief 调用绑定的日志输出回调函数
         *
         * @param msg 格式化完成的完整日志消息文本
         */
        void operator()(std::string_view msg) const
        {
            if (fn != nullptr)
                fn(ctx, msg);
        }

        /**
         * @brief 判断输出回调是否有效配置
         */
        explicit constexpr operator bool() const noexcept { return fn != nullptr; }
    };

    /**
     * @brief 日志强制刷盘回调封装结构体
     */
    struct FlushFunction
    {
        using Fn = void (*)(void *ctx);

        Fn fn = nullptr;
        void *ctx = nullptr;

        constexpr FlushFunction() noexcept = default;
        constexpr FlushFunction(Fn f, void *c = nullptr) noexcept
            : fn(f),
              ctx(c)
        {
        }

        /**
         * @brief 调用绑定的强制刷盘回调函数
         */
        void operator()() const
        {
            if (fn != nullptr)
                fn(ctx);
        }

        /**
         * @brief 判断刷盘回调是否有效配置
         */
        explicit constexpr operator bool() const noexcept { return fn != nullptr; }
    };

    namespace detail
    {
        /**
         * @brief 默认控制台输出实现（输出至 stdout）
         *
         * @param ctx 上下文指针（未使用）
         * @param msg 待输出的日志文本
         */
        inline void stdoutOutput(void * /*ctx*/, std::string_view msg)
        {
            if (!msg.empty())
                std::fwrite(msg.data(), 1, msg.size(), stdout);
        }

        /**
         * @brief 默认控制台强制刷盘实现（刷新 stdout）
         *
         * @param ctx 上下文指针（未使用）
         */
        inline void stdoutFlush(void * /*ctx*/)
        {
            std::fflush(stdout);
        }

        /**
         * @brief 输出缓冲区事务卫述类（RAII）
         *
         * 纯 RAII 实现，无任何 try/catch 依赖，零异常开销，天然兼容 -fno-exceptions。
         * 保证在非正常提前退出或栈展开时，自动回滚缓冲区已写入字节，
         * 并妥善释放所获取的线程缓冲区（release 0 字节），避免脏前缀残留。
         */
        struct OutputBufferGuard
        {
            const OutputFunction &fn;
            std::string &buf;
            const size_t origSize;
            const bool isAcquired;
            bool committed = false;

            ~OutputBufferGuard() noexcept
            {
                if (!committed) [[unlikely]]
                {
                    buf.resize(origSize);
                    if (isAcquired && fn.release != nullptr)
                    {
                        fn.release(fn.ctx, 0);
                    }
                }
            }
        };

        /**
         * @brief 将日志级别枚举映射为前缀文本字符串
         *
         * @param lv2str 指向级别字符串数组的指针
         * @param level 日志级别枚举
         * @return const char* 对应的级别文本标签
         */
        inline const char *levelToStr(const std::array<const char *, kLevelCount> *lv2str, LEVEL level) noexcept
        {
            const size_t idx = static_cast<size_t>(level);
            if (idx >= kLevelCount) [[unlikely]]
                return "UNKNOWN";
            return (*lv2str)[idx];
        }

        inline constexpr size_t kMinMaxLogLength = 1ULL << 6;
        inline constexpr size_t kMaxMaxLogLength = 1ULL << 26;

        inline constexpr int kActiveLevel = MYLOG_ACTIVE_LEVEL;

        /**
         * @brief 数值范围钳位函数
         *
         * @tparam T 数值类型
         * @param value 输入值
         * @param lo 允许的下界
         * @param hi 允许的上界
         * @return constexpr T 限制在 [lo, hi] 内的有效值
         */
        template <typename T>
        constexpr T clampValue(T value, T lo, T hi) noexcept
        {
            return value < lo ? lo : (value > hi ? hi : value);
        }

        /**
         * @brief 快速十进制数字填充至定长缓冲区
         *
         * @tparam Int 整数类型
         * @param dst 目标缓冲区起始地址
         * @param value 待写入的非负整数
         * @param width 目标填充宽度（不足则高位补 '0'）
         */
        template <typename Int>
        inline void putDigits(char *dst, Int value, int width) noexcept
        {
            for (int i = width - 1; i >= 0; --i)
            {
                dst[i] = static_cast<char>('0' + value % 10);
                value /= 10;
            }
        }

        /**
         * @brief 格式化并追加纳秒级高精度时间戳
         *
         * 采用基于 thread_local 秒级文本缓存与 putDigits 快速除法的高性能时间格式化算法：
         * 同一秒内的多次日志打印直接复用预格式化好的秒级前缀（含小数点），消除频繁调用
         * localtime_r / gmtime_r 带来的锁竞争与内核开销。
         * 热路径零中间栈缓冲，原位直接填充 29 字节纳秒时间戳（YYYY-MM-DD HH:MM:SS.nnnnnnnnn）。
         *
         * @param out 输出目标字符串引用
         * @param utc 是否使用 UTC 时间（false 为本地时区）
         */
        inline void appendTimestamp(std::string &out, bool utc = false)
        {
            using namespace std::chrono;
            const auto now = system_clock::now();
            const auto secs = floor<seconds>(now);
            const std::time_t s = system_clock::to_time_t(secs);

            static thread_local struct TimestampCache
            {
                std::time_t second = static_cast<std::time_t>(-1);
                bool wasUtc = false;
                char text[21] = "0000-00-00 00:00:00.";
            } cache;
            if (s != cache.second || utc != cache.wasUtc) [[unlikely]]
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
                p[19] = '.';
                p[20] = '\0';
                cache.second = s;
                cache.wasUtc = utc;
            }

            const size_t sz = out.size();
            out.resize(sz + 29);
            char *dst = out.data() + sz;
            std::memcpy(dst, cache.text, 20);
            const auto nanos = duration_cast<nanoseconds>(now - secs).count();
            putDigits(dst + 20, clampValue<int64_t>(nanos, 0, 999999999), 9);
        }

        /**
         * @brief 格式化并追加通用整型数值
         *
         * @tparam Int 整数类型
         * @param out 输出目标字符串引用
         * @param value 待写入的数值
         */
        template <typename Int>
        inline void appendInt(std::string &out, Int value)
        {
            char buf[24];
            const auto res = std::to_chars(buf, buf + sizeof(buf), value);
            out.append(buf, static_cast<size_t>(res.ptr - buf));
        }

        /**
         * @brief 极速格式化并追加非负正整数（如行号 line）
         *
         * 从后向前填充，无需调用库函数，分支预测极度友好。
         *
         * @param out 输出目标字符串引用
         * @param value 待写入的非负正整数
         */
        inline void appendPositiveInt(std::string &out, int value) noexcept
        {
            char buf[12];
            char *p = buf + sizeof(buf);
            unsigned u = static_cast<unsigned>(value);
            do
            {
                *--p = static_cast<char>('0' + (u % 10));
                u /= 10;
            } while (u > 0);
            out.append(p, static_cast<size_t>(buf + sizeof(buf) - p));
        }

        /**
         * @brief 获取当前线程的原生操作系统线程 ID
         *
         * 缓存于 thread_local 中，避免每次记录日志均发起 gettid 系统调用。
         *
         * @return long long 线程 ID
         */
        inline long long getThreadId() noexcept
        {
            static thread_local const long long tid = static_cast<long long>(::gettid());
            return tid;
        }

        /**
         * @brief 获取预格式化好的当前线程 ID 标签前缀
         *
         * 格式为："[tid]"（例如 "[1234]"），在各线程首次记录日志时初始化一次并永久缓存。
         *
         * @return std::string_view 线程标签前缀视图
         */
        inline std::string_view getCachedThreadIdPrefix() noexcept
        {
            static thread_local struct TidPrefix
            {
                char text[24];
                size_t len = 0;
                TidPrefix() noexcept
                {
                    text[0] = '[';
                    const auto res =
                        std::to_chars(text + 1, text + sizeof(text) - 1, static_cast<long long>(::gettid()));
                    *res.ptr = ']';
                    len = static_cast<size_t>(res.ptr + 1 - text);
                }
            } cache;
            return {cache.text, cache.len};
        }

        /**
         * @brief 基于 64 位 SWAR 位运算快速检测文本中是否存在需要转义的控制字符
         *
         * 每次以 8 字节为一组比对 ascii < 0x20 或 == 0x7f，
         * 对于绝大多数正常日志字符串可实现接近内存带宽的高速无分支扫描。
         *
         * @param data 字符数组指针
         * @param size 字符长度
         * @return true 存在需要转义的控制字符
         * @return false 不含任何控制字符，可直接批量输出
         */
        inline bool checkNeedsEscape(const char *data, size_t size) noexcept
        {
            size_t i = 0;
            while (i + 8 <= size)
            {
                uint64_t word;
                std::memcpy(&word, data + i, 8);
                uint64_t sub = word - 0x2020202020202020ULL;
                uint64_t xor7f = word ^ 0x7f7f7f7f7f7f7f7fULL;
                uint64_t sub7f = (xor7f - 0x0101010101010101ULL) & ~xor7f & 0x8080808080808080ULL;
                if ((sub & 0x8080808080808080ULL) || sub7f)
                {
                    for (size_t k = 0; k < 8; ++k)
                    {
                        const unsigned char c = static_cast<unsigned char>(data[i + k]);
                        if (c < 0x20 || c == 0x7f)
                            return true;
                    }
                }
                i += 8;
            }
            for (; i < size; ++i)
            {
                const unsigned char c = static_cast<unsigned char>(data[i]);
                if (c < 0x20 || c == 0x7f)
                    return true;
            }
            return false;
        }

        /**
         * @brief 将含有控制字符的文本转义并追加至输出缓冲区
         *
         * 特殊转换：'\n' -> "\n", '\r' -> "\r", '\033' -> "\e"，其余控制字符格式化为 "\xHH"。
         *
         * @param out 输出目标字符串引用
         * @param sv 待转义的原始字符串视图
         */
        inline void appendEscaped(std::string &out, std::string_view sv)
        {
            if (sv.empty())
                return;
            if (!checkNeedsEscape(sv.data(), sv.size()))
            {
                out.append(sv.data(), sv.size());
                return;
            }
            static constexpr char kHexDigits[] = "0123456789abcdef";
            for (char c : sv)
            {
                switch (c)
                {
                case '\n':
                    out.append("\\n", 2);
                    break;
                case '\r':
                    out.append("\\r", 2);
                    break;
                case '\033':
                    out.append("\\e", 2);
                    break;
                default:
                {
                    const unsigned char uc = static_cast<unsigned char>(c);
                    if (uc < 0x20 || uc == 0x7f)
                    {
                        const char buf[4] = {'\\', 'x', kHexDigits[(uc >> 4) & 0xF], kHexDigits[uc & 0xF]};
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

    inline constexpr std::array<const char *, kLevelCount> LV2STR = {" TRACE ", " DEBUG ", " INFO ",
                                                                     " WARN ",  " ERROR ", " FATAL "};
    inline constexpr std::array<const char *, kLevelCount> LV2STR_VIVID = {
        "\033[7;37m TRACE \033[0m", "\033[7;36m DEBUG \033[0m", "\033[7;32m INFO \033[0m",
        "\033[7;33m WARN \033[0m",  "\033[7;31m ERROR \033[0m", "\033[5;41m FATAL \033[0m"};

    /**
     * @brief 核心日志引擎单例类
     *
     * 负责日志格式化、控制字符转义、时间戳拼接、线程安全输出与 FATAL 动作处理。
     */
    class Logger
    {
    public:
        /**
         * @brief 日志引擎运行配置
         */
        struct Config
        {
            const std::array<const char *, kLevelCount> *lv2str = &LV2STR;
            FATAL_ACTION fatalAction = FATAL_ACTION::NONE;
            bool escapeControlChars = true;
            bool timestampUtc = false;
            size_t maxLogLength = 1ULL << 20;
            OutputFunction outputFunction{&detail::stdoutOutput, nullptr};
            FlushFunction flushFunction{&detail::stdoutFlush, nullptr};

            /**
             * @brief 规范化配置各项阈值至安全范围内
             */
            void normalize() noexcept
            {
                maxLogLength = std::min(std::max(maxLogLength, detail::kMinMaxLogLength), detail::kMaxMaxLogLength);
                if (lv2str == nullptr)
                    lv2str = &LV2STR;
            }
        };

        /**
         * @brief 获取当前日志引擎的配置快照
         *
         * @return Config 当前生效的配置副本
         */
        Config getConfig() const noexcept { return refConfig(); }

        /**
         * @brief 更新并发布新的日志引擎配置
         *
         * @param cfg 新的配置对象（自动进行规范化处理）
         */
        void setConfig(Config cfg)
        {
            cfg.normalize();
            cfgHolder_.publish(std::move(cfg));
        }

        /**
         * @brief 获取 Logger 全局单例
         *
         * @return Logger& 单例对象引用
         */
        inline static Logger &getInstance() noexcept
        {
            static Logger *instance = new Logger();
            return *instance;
        }

        /**
         * @brief 设置自定义日志输出回调
         *
         * @param fn 输出回调结构体
         */
        void setOutputFunction(OutputFunction fn)
        {
            Config cfg = getConfig();
            cfg.outputFunction = fn;
            setConfig(std::move(cfg));
        }

        /**
         * @brief 设置自定义强制刷盘回调
         *
         * @param fn 刷盘回调结构体
         */
        void setFlushFunction(FlushFunction fn)
        {
            Config cfg = getConfig();
            cfg.flushFunction = fn;
            setConfig(std::move(cfg));
        }

        /**
         * @brief 获取当前生效的日志抑制门限级别
         *
         * 低于此级别的日志将被静默过滤丢弃。
         *
         * @return LEVEL 当前抑制门限级别
         */
        LEVEL getSuppressLevel() const noexcept
        {
            return static_cast<LEVEL>(suppressLevel_.load(std::memory_order_relaxed));
        }

        /**
         * @brief 设置日志抑制门限级别（枚举参数）
         *
         * @param level 新的门限级别
         */
        void setSuppressLevel(LEVEL level) noexcept
        {
            suppressLevel_.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
        }

        /**
         * @brief 设置日志抑制门限级别（字符串参数）
         *
         * @param levelStr 级别名称字符串（如 "DEBUG", "INFO", "WARN" 等，忽略大小写）
         * @return true 字符串识别成功且已设置
         * @return false 未知级别字符串，未作修改
         */
        bool setSuppressLevel(std::string_view levelStr) noexcept
        {
            if (auto lvl = parseLevel(levelStr))
            {
                setSuppressLevel(*lvl);
                return true;
            }
            return false;
        }

        /**
         * @brief 格式化并输出结构化日志
         *
         * 执行完整的日志流水线：
         * 1. 递归重入拦截检测（ReentrancyGuard）；
         * 2. 尝试从输出后端直接借出缓冲区（零拷贝优化）；
         * 3. 拼接纳秒级缓存时间戳、级别标签、缓存线程 ID、调用函数名；
         * 4. 基于 std::format 进行格式化求值；
         * 5. 校验最大长度并按需截断；
         * 6. 控制字符 SWAR 快速检测与转义；
         * 7. 拼接源码文件名与行号；
         * 8. 交付后端输出，并在 FATAL 级别触发强制刷盘与退出处理。
         *
         * @tparam Args 格式化参数类型包
         * @param level 日志级别
         * @param file 调用源文件名
         * @param line 调用源行号
         * @param func 调用所在函数名
         * @param fmt std::format 兼容的格式化字符串
         * @param args 可变格式化参数
         */
        template <typename... Args>
        inline void logFmt(LEVEL level, const char *file, int line, const char *func, std::format_string<Args...> fmt,
                           Args &&...args)
        {
            thread_local bool inLog = false;
            if (inLog) [[unlikely]]
                return;
            struct ReentrancyGuard
            {
                bool &flag;
                explicit ReentrancyGuard(bool &f) noexcept
                    : flag(f)
                {
                    flag = true;
                }
                ~ReentrancyGuard() noexcept { flag = false; }
            } guard{inLog};

            const Config &cfg = refConfig();

            std::string *destBuf = nullptr;
            if (cfg.outputFunction.acquire != nullptr) [[likely]]
            {
                destBuf = cfg.outputFunction.acquire(cfg.outputFunction.ctx);
            }

            static thread_local struct TlsBuffer
            {
                std::string buf;
                TlsBuffer() { buf.reserve(512); }
            } tls;

            std::string &buf = (destBuf != nullptr) ? *destBuf : tls.buf;
            const size_t logStart = (destBuf != nullptr) ? buf.size() : 0;
            if (destBuf == nullptr)
                buf.clear();

            detail::OutputBufferGuard bufGuard{cfg.outputFunction, buf, logStart, destBuf != nullptr};

            detail::appendTimestamp(buf, cfg.timestampUtc);
            buf.append(detail::levelToStr(cfg.lv2str, level));
            buf.append(detail::getCachedThreadIdPrefix());
            buf.append(func);
            buf.append(">>", 2);

            const size_t msgStart = buf.size();
            if constexpr (sizeof...(Args) == 0)
            {
                buf.append(fmt.get());
            }
            else
            {
                std::format_to(std::back_inserter(buf), fmt, std::forward<Args>(args)...);
            }

            size_t msgLen = buf.size() - msgStart;
            if (msgLen > cfg.maxLogLength) [[unlikely]]
            {
                buf.resize(msgStart + cfg.maxLogLength);
                msgLen = cfg.maxLogLength;
            }

            if (cfg.escapeControlChars && detail::checkNeedsEscape(buf.data() + msgStart, msgLen)) [[unlikely]]
            {
                thread_local std::string scratch;
                scratch.assign(buf.data() + msgStart, msgLen);
                buf.resize(msgStart);
                detail::appendEscaped(buf, scratch);
                if (buf.size() - msgStart > cfg.maxLogLength) [[unlikely]]
                    buf.resize(msgStart + cfg.maxLogLength);
            }

            buf.append(" (", 2);
            buf.append(file);
            buf.push_back(':');
            detail::appendPositiveInt(buf, line);
            buf.append(")\n", 2);

            const size_t written = buf.size() - logStart;
            bufGuard.committed = true;
            if (destBuf != nullptr)
            {
                cfg.outputFunction.release(cfg.outputFunction.ctx, written);
            }
            else
            {
                cfg.outputFunction(std::string_view(buf.data(), written));
            }

            if (level == LEVEL::FATAL) [[unlikely]]
            {
                cfg.flushFunction();
                std::fflush(nullptr);
                switch (cfg.fatalAction)
                {
                case FATAL_ACTION::NONE:
                    break;
                case FATAL_ACTION::EXIT:
                    std::exit(EXIT_FAILURE);
                case FATAL_ACTION::ABORT:
                    std::abort();
                }
            }
        }

        /**
         * @brief 输出非格式化纯文本日志消息
         *
         * @param level 日志级别
         * @param file 调用源文件名
         * @param line 调用源行号
         * @param func 调用所在函数名
         * @param message 纯字符串消息内容视图
         */
        inline void log(LEVEL level, const char *file, int line, const char *func, std::string_view message)
        {
            thread_local bool inLog = false;
            if (inLog) [[unlikely]]
                return;
            struct ReentrancyGuard
            {
                bool &flag;
                explicit ReentrancyGuard(bool &f) noexcept
                    : flag(f)
                {
                    flag = true;
                }
                ~ReentrancyGuard() noexcept { flag = false; }
            } guard{inLog};

            const Config &cfg = refConfig();

            if (message.size() > cfg.maxLogLength) [[unlikely]]
                message = message.substr(0, cfg.maxLogLength);

            std::string *destBuf = nullptr;
            if (cfg.outputFunction.acquire != nullptr) [[likely]]
            {
                destBuf = cfg.outputFunction.acquire(cfg.outputFunction.ctx);
            }

            static thread_local struct TlsBuffer
            {
                std::string buf;
                TlsBuffer() { buf.reserve(512); }
            } tls;

            std::string &buf = (destBuf != nullptr) ? *destBuf : tls.buf;
            const size_t logStart = (destBuf != nullptr) ? buf.size() : 0;
            if (destBuf == nullptr)
                buf.clear();

            detail::OutputBufferGuard bufGuard{cfg.outputFunction, buf, logStart, destBuf != nullptr};

            detail::appendTimestamp(buf, cfg.timestampUtc);
            buf.append(detail::levelToStr(cfg.lv2str, level));
            buf.append(detail::getCachedThreadIdPrefix());
            buf.append(func);
            buf.append(">>", 2);
            if (cfg.escapeControlChars)
            {
                const size_t msgStart = buf.size();
                detail::appendEscaped(buf, message);
                if (buf.size() - msgStart > cfg.maxLogLength) [[unlikely]]
                    buf.resize(msgStart + cfg.maxLogLength);
            }
            else if (!message.empty())
                buf.append(message.data(), message.size());
            buf.append(" (", 2);
            buf.append(file);
            buf.push_back(':');
            detail::appendPositiveInt(buf, line);
            buf.append(")\n", 2);

            const size_t written = buf.size() - logStart;
            bufGuard.committed = true;
            if (destBuf != nullptr)
            {
                cfg.outputFunction.release(cfg.outputFunction.ctx, written);
            }
            else
            {
                cfg.outputFunction(std::string_view(buf.data(), written));
            }

            if (level == LEVEL::FATAL) [[unlikely]]
            {
                cfg.flushFunction();
                std::fflush(nullptr);
                switch (cfg.fatalAction)
                {
                case FATAL_ACTION::NONE:
                    break;
                case FATAL_ACTION::EXIT:
                    std::exit(EXIT_FAILURE);
                case FATAL_ACTION::ABORT:
                    std::abort();
                }
            }
        }

    private:
        ConfigHolder<Config> cfgHolder_;
        std::atomic<uint8_t> suppressLevel_{static_cast<uint8_t>(LEVEL::TRACE)};

        /**
         * @brief 获取当前线程私有的最新配置快照引用
         */
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

    /**
     * @brief 获取当前全局生效的日志抑制级别门限
     *
     * @return LEVEL 当前抑制门限级别
     */
    inline LEVEL getCurrentSuppressLevel() noexcept
    {
        return Logger::getInstance().getSuppressLevel();
    }

    /**
     * @brief 设置全局日志抑制级别门限（枚举重载）
     *
     * @param level 新的门限级别
     */
    inline void setSuppressLevel(LEVEL level) noexcept
    {
        Logger::getInstance().setSuppressLevel(level);
    }

    /**
     * @brief 设置全局日志抑制级别门限（字符串重载）
     *
     * @param levelStr 级别名称字符串（忽略大小写）
     * @return true 设置成功
     * @return false 级别名称无效
     */
    inline bool setSuppressLevel(std::string_view levelStr) noexcept
    {
        return Logger::getInstance().setSuppressLevel(levelStr);
    }

    /**
     * @brief 底层统一日志分发接口（格式化模板重载）
     */
    template <typename... Args>
    inline void logger(LEVEL level, const char *file, int line, const char *func, std::format_string<Args...> fmt,
                       Args &&...args)
    {
        Logger::getInstance().logFmt(level, file, line, func, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 底层统一日志分发接口（字符串视图重载）
     */
    inline void logger(LEVEL level, const char *file, int line, const char *func, std::string_view fmt)
    {
        Logger::getInstance().log(level, file, line, func, fmt);
    }

    /**
     * @brief 底层统一日志分发接口（空消息重载）
     */
    inline void logger(LEVEL level, const char *file, int line, const char *func)
    {
        Logger::getInstance().log(level, file, line, func, {});
    }

    /**
     * @brief 自动捕获调用位置的格式化字符串包装类
     *
     * 利用 std::source_location::current() 实现零宏、类型安全的源码位置自动注入。
     *
     * @tparam Args 格式化参数类型包
     */
    template <typename... Args>
    struct FormatWithLocation
    {
        std::format_string<Args...> fmt;
        std::source_location loc;

        template <typename S>
            requires std::constructible_from<std::format_string<Args...>, S>
        consteval FormatWithLocation(const S &s, std::source_location l = std::source_location::current()) noexcept
            : fmt(s),
              loc(l)
        {
        }
    };

    /**
     * @brief 现代 C++ 日志通用分发入口（格式化重载）
     *
     * 1. 编译期静态过滤：若 Level < MYLOG_ACTIVE_LEVEL，编译器直接消除调用代码，零运行期开销；
     * 2. 运行期动态抑制：校验当前 suppressLevel，未达标则无锁跳过。
     *
     * @tparam Level 日志级别
     * @tparam Args 格式化参数类型
     * @param format 格式化字符串及其关联源码位置
     * @param args 可变格式化参数
     */
    template <LEVEL Level, typename... Args>
    inline void log(FormatWithLocation<std::type_identity_t<Args>...> format, Args &&...args)
    {
        if constexpr (static_cast<int>(Level) >= detail::kActiveLevel)
        {
            if (Level >= getCurrentSuppressLevel())
            {
                Logger::getInstance().logFmt(Level, format.loc.file_name(), static_cast<int>(format.loc.line()),
                                             format.loc.function_name(), format.fmt, std::forward<Args>(args)...);
            }
        }
    }

    /**
     * @brief 现代 C++ 日志通用分发入口（字符串视图重载）
     *
     * @tparam Level 日志级别
     * @param msg 纯文本日志消息
     * @param loc 自动捕获的源码位置
     */
    template <LEVEL Level>
    inline void log(std::string_view msg, std::source_location loc = std::source_location::current())
    {
        if constexpr (static_cast<int>(Level) >= detail::kActiveLevel)
        {
            if (Level >= getCurrentSuppressLevel())
            {
                Logger::getInstance().log(Level, loc.file_name(), static_cast<int>(loc.line()), loc.function_name(),
                                          msg);
            }
        }
    }

    /**
     * @brief 现代 C++ 日志通用分发入口（自定义可反射/可格式化对象重载）
     *
     * 允许直接传入聚合结构体、枚举类或支持 std::formatter 的自定义对象进行打印。
     *
     * @tparam Level 日志级别
     * @tparam T 对象类型
     * @param val 待打印的对象实例
     * @param loc 自动捕获的源码位置
     */
    template <LEVEL Level, typename T>
        requires(!std::is_convertible_v<T, std::string_view>)
    inline void log(const T &val, std::source_location loc = std::source_location::current())
    {
        if constexpr (static_cast<int>(Level) >= detail::kActiveLevel)
        {
            if (Level >= getCurrentSuppressLevel())
            {
                Logger::getInstance().logFmt(Level, loc.file_name(), static_cast<int>(loc.line()), loc.function_name(),
                                             "{}", val);
            }
        }
    }

    // =========================================================================
    // 用户级现代无宏便捷 API (保持用户原名：trace, debug, info, warn, error, fatal)
    // =========================================================================

    /**
     * @brief 记录 TRACE 级别格式化日志
     */
    template <typename... Args>
    inline void trace(FormatWithLocation<std::type_identity_t<Args>...> format, Args &&...args)
    {
        log<LEVEL::TRACE>(format, std::forward<Args>(args)...);
    }
    /**
     * @brief 记录 TRACE 级别纯文本日志
     */
    inline void trace(std::string_view msg, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::TRACE>(msg, loc);
    }
    /**
     * @brief 记录 TRACE 级别对象日志
     */
    template <typename T>
        requires(!std::is_convertible_v<T, std::string_view>)
    inline void trace(const T &val, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::TRACE>(val, loc);
    }

    /**
     * @brief 记录 DEBUG 级别格式化日志
     */
    template <typename... Args>
    inline void debug(FormatWithLocation<std::type_identity_t<Args>...> format, Args &&...args)
    {
        log<LEVEL::DEBUG>(format, std::forward<Args>(args)...);
    }
    /**
     * @brief 记录 DEBUG 级别纯文本日志
     */
    inline void debug(std::string_view msg, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::DEBUG>(msg, loc);
    }
    /**
     * @brief 记录 DEBUG 级别对象日志
     */
    template <typename T>
        requires(!std::is_convertible_v<T, std::string_view>)
    inline void debug(const T &val, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::DEBUG>(val, loc);
    }

    /**
     * @brief 记录 INFO 级别格式化日志
     */
    template <typename... Args>
    inline void info(FormatWithLocation<std::type_identity_t<Args>...> format, Args &&...args)
    {
        log<LEVEL::INFO>(format, std::forward<Args>(args)...);
    }
    /**
     * @brief 记录 INFO 级别纯文本日志
     */
    inline void info(std::string_view msg, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::INFO>(msg, loc);
    }
    /**
     * @brief 记录 INFO 级别对象日志
     */
    template <typename T>
        requires(!std::is_convertible_v<T, std::string_view>)
    inline void info(const T &val, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::INFO>(val, loc);
    }

    /**
     * @brief 记录 WARN 级别格式化日志
     */
    template <typename... Args>
    inline void warn(FormatWithLocation<std::type_identity_t<Args>...> format, Args &&...args)
    {
        log<LEVEL::WARN>(format, std::forward<Args>(args)...);
    }
    /**
     * @brief 记录 WARN 级别纯文本日志
     */
    inline void warn(std::string_view msg, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::WARN>(msg, loc);
    }
    /**
     * @brief 记录 WARN 级别对象日志
     */
    template <typename T>
        requires(!std::is_convertible_v<T, std::string_view>)
    inline void warn(const T &val, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::WARN>(val, loc);
    }

    /**
     * @brief 记录 ERROR 级别格式化日志
     */
    template <typename... Args>
    inline void error(FormatWithLocation<std::type_identity_t<Args>...> format, Args &&...args)
    {
        log<LEVEL::ERROR>(format, std::forward<Args>(args)...);
    }
    /**
     * @brief 记录 ERROR 级别纯文本日志
     */
    inline void error(std::string_view msg, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::ERROR>(msg, loc);
    }
    /**
     * @brief 记录 ERROR 级别对象日志
     */
    template <typename T>
        requires(!std::is_convertible_v<T, std::string_view>)
    inline void error(const T &val, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::ERROR>(val, loc);
    }

    /**
     * @brief 记录 FATAL 级别格式化日志
     */
    template <typename... Args>
    inline void fatal(FormatWithLocation<std::type_identity_t<Args>...> format, Args &&...args)
    {
        log<LEVEL::FATAL>(format, std::forward<Args>(args)...);
    }
    /**
     * @brief 记录 FATAL 级别纯文本日志
     */
    inline void fatal(std::string_view msg, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::FATAL>(msg, loc);
    }
    /**
     * @brief 记录 FATAL 级别对象日志
     */
    template <typename T>
        requires(!std::is_convertible_v<T, std::string_view>)
    inline void fatal(const T &val, std::source_location loc = std::source_location::current())
    {
        log<LEVEL::FATAL>(val, loc);
    }

} // namespace mylog

#if !MYLOG_HAS_REFLECTION
template <>
struct std::formatter<mylog::LEVEL> : std::formatter<std::string_view>
{
    auto format(mylog::LEVEL val, std::format_context &ctx) const
    {
        return std::formatter<std::string_view>::format(mylog::levelToString(val), ctx);
    }
};

template <>
struct std::formatter<mylog::FATAL_ACTION> : std::formatter<std::string_view>
{
    auto format(mylog::FATAL_ACTION val, std::format_context &ctx) const
    {
        return std::formatter<std::string_view>::format(mylog::fatalActionToString(val), ctx);
    }
};
#endif

#define MYLOG_LOG_IMPL(lv, ...)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if constexpr (static_cast<int>(::mylog::LEVEL::lv) >= ::mylog::detail::kActiveLevel)                           \
        {                                                                                                              \
            if (::mylog::LEVEL::lv >= ::mylog::getCurrentSuppressLevel())                                              \
            {                                                                                                          \
                ::mylog::logger(::mylog::LEVEL::lv, __FILE__, __LINE__, __func__ __VA_OPT__(, ) __VA_ARGS__);          \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define LOG_TRACE(...) MYLOG_LOG_IMPL(TRACE, __VA_ARGS__)
#define LOG_DEBUG(...) MYLOG_LOG_IMPL(DEBUG, __VA_ARGS__)
#define LOG_INFO(...) MYLOG_LOG_IMPL(INFO, __VA_ARGS__)
#define LOG_WARN(...) MYLOG_LOG_IMPL(WARN, __VA_ARGS__)
#define LOG_ERROR(...) MYLOG_LOG_IMPL(ERROR, __VA_ARGS__)
#define LOG_FATAL(...) MYLOG_LOG_IMPL(FATAL, __VA_ARGS__)
