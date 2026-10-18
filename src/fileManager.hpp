#pragma once

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace mylog
{
    namespace detail
    {
        inline constexpr size_t kMinFileBufferCapacity = 1ULL << 10;
        inline constexpr size_t kMaxFileBufferCapacity = 256ULL << 20;
        inline constexpr size_t kMaxThreadBufferCapacity = 64ULL << 20;
        inline constexpr size_t kMinFileCapacity = 1ULL << 10;
        inline constexpr size_t kMaxFileCapacity = 64ULL << 30;
        inline constexpr size_t kMaxIntervalFlushMs = 24ULL * 60 * 60 * 1000;
        inline constexpr size_t kMaxBasenameLength = 1ULL << 7;
        inline constexpr size_t kMaxMaxFiles = 1ULL << 10;
    } // namespace detail

} // namespace mylog

#include "configHolder.hpp"
#include "logger.hpp"

namespace mylog
{

    namespace detail
    {
        /**
         * @brief 生成纳秒级高精度时间戳字符串
         *
         * 格式为：YYYYMMDD_HHMMSS_nnnnnnnnn（例如：20261018_203000_123456789），
         * 用于确保日志滚动生成的新文件名在微秒/纳秒并发下唯一且按字典序自增。
         *
         * @return std::string 格式化后的时间戳字符串
         */
        inline std::string getTimestampString()
        {
            const auto now = std::chrono::system_clock::now();
            const auto secs = std::chrono::floor<std::chrono::seconds>(now);
            const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now - secs);
            return std::format("{:%Y%m%d_%H%M%S}_{:09}", secs, nanos.count());
        }

        /**
         * @brief 异常降级输出至标准输出
         *
         * 当日志文件描述符失效、磁盘空间耗尽或写盘产生致命错误时，
         * 将日志数据无条件降级打印至 stdout 并强制刷新，杜绝日志静默丢失。
         *
         * @param data 待输出的数据块
         */
        inline void writeFallback(std::string_view data) noexcept
        {
            if (data.empty())
                return;
            std::fwrite(data.data(), 1, data.size(), stdout);
            std::fflush(stdout);
        }

        /**
         * @brief 健壮的 POSIX 循环系统调用写盘
         *
         * 循环调用 write(2) 直至全部数据写完；自动捕获并重试被操作系统信号中断的调用 (EINTR)。
         * 若遇到致命系统错误或写盘返回 0，则自动将剩余数据降级分流至 writeFallback。
         *
         * @param fd 目标文件的文件描述符
         * @param data 待写入的数据视图
         * @return size_t 实际成功写入目标 fd 的总字节数
         */
        inline size_t writeFile(int fd, std::string_view data) noexcept
        {
            if (fd < 0)
            {
                writeFallback(data);
                return 0;
            }
            const char *p = data.data();
            size_t n = data.size();
            size_t done = 0;
            while (n > 0)
            {
                const ssize_t written = ::write(fd, p, n);
                if (written < 0)
                {
                    if (errno == EINTR)
                        continue;
                    writeFallback({p, n});
                    break;
                }
                if (written == 0)
                {
                    writeFallback({p, n});
                    break;
                }
                p += written;
                n -= static_cast<size_t>(written);
                done += static_cast<size_t>(written);
            }
            return done;
        }

        /**
         * @brief 安全打开或创建日志追加文件
         *
         * 使用标志位：O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC
         * - O_APPEND: 内核级原子追加写定位，消除多线程追加竞争
         * - O_NOFOLLOW: 拒绝跟踪符号链接，规避软链接替换等特权越权攻击漏洞
         * - O_CLOEXEC: 子进程 fork/exec 时自动关闭该 fd，防止 fd 泄漏
         *
         * @param path 目标文件路径
         * @return int 成功返回有效文件描述符 (>= 0)，失败返回 -1
         */
        inline int openAppendNoFollow(const char *path) noexcept
        {
            return ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0644);
        }

        /**
         * @brief 获取已打开文件描述符的当前文件大小
         *
         * 通过 fstat 系统调用读取文件当前的 st_size 字节数。
         *
         * @param fd 文件描述符
         * @return size_t 文件当前字节大小，失败返回 0
         */
        inline size_t getFileSize(int fd) noexcept
        {
            struct stat st{};
            return (::fstat(fd, &st) == 0) ? static_cast<size_t>(st.st_size) : 0;
        }

        /// @brief 兼容性别名
        inline size_t fileSizeOf(int fd) noexcept
        {
            return getFileSize(fd);
        }

        /**
         * @brief 检查指定路径是否为常规文件（Regular File）
         *
         * @param path 文件路径
         * @return true 目标存在且为普通文件
         * @return false 目标不存在、为目录或为特殊设备文件
         */
        inline bool isRegularFile(const char *path) noexcept
        {
            struct stat st{};
            return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
        }

        /**
         * @brief 生成共享单文件模式下的新日志文件名
         *
         * 格式为：{时间戳}_{basename}_{pid}.log
         *
         * @param basename 日志文件基名
         * @return std::string 生成的完整文件名
         */
        inline std::string getFilenameSingle(const char *basename)
        {
            return std::format("{}_{}_{}.log", getTimestampString(), basename, static_cast<long long>(::getpid()));
        }

        /**
         * @brief 生成共享单文件模式下的文件匹配后缀
         *
         * 格式为：_{basename}_{pid}.log，用于扫描和淘汰归属于本进程该基名的历史文件。
         *
         * @param basename 日志文件基名
         * @return std::string 后缀字符串
         */
        inline std::string getSingleFileSuffix(std::string_view basename)
        {
            return "_" + std::string(basename) + "_" + std::to_string(static_cast<long long>(::getpid())) + ".log";
        }

        /// @brief 兼容性别名
        inline std::string singleSuffix(std::string_view basename)
        {
            return getSingleFileSuffix(basename);
        }

        /**
         * @brief 生成每线程独立文件模式下的新日志文件名
         *
         * 格式为：{时间戳}_{basename}_{pid}_{tid}.log
         *
         * @param basename 日志文件基名
         * @param tid 线程原生 ID
         * @return std::string 生成的完整文件名
         */
        inline std::string getFilenameMultiple(const char *basename, long long tid)
        {
            return std::format("{}_{}_{}_{}.log", getTimestampString(), basename, static_cast<long long>(::getpid()),
                               tid);
        }

        /**
         * @brief 生成每线程独立文件模式下的文件匹配后缀
         *
         * 格式为：_{basename}_{pid}_{tid}.log
         *
         * @param basename 日志文件基名
         * @param tid 线程原生 ID
         * @return std::string 后缀字符串
         */
        inline std::string getMultipleFileSuffix(std::string_view basename, long long tid)
        {
            return "_" + std::string(basename) + "_" + std::to_string(static_cast<long long>(::getpid())) + "_" +
                   std::to_string(tid) + ".log";
        }

        /// @brief 兼容性别名
        inline std::string multipleSuffix(std::string_view basename, long long tid)
        {
            return getMultipleFileSuffix(basename, tid);
        }

        /**
         * @brief 扫描当前目录下匹配指定后缀的所有候选日志文件
         *
         * 遍历目录并收集文件名字尾匹配目标后缀的文件，并按字典序从小到大（从旧到新）升序排列。
         *
         * @param suffix 目标文件后缀
         * @return std::vector<std::string> 排序后的历史文件列表
         */
        inline std::vector<std::string> scanCandidateFiles(const std::string &suffix)
        {
            namespace fs = std::filesystem;
            std::vector<std::string> names;
            std::error_code ec;
            fs::directory_iterator it(".", ec);
            const fs::directory_iterator end;
            while (!ec && it != end)
            {
                std::error_code fileEc;
                if (it->is_regular_file(fileEc))
                {
                    const std::string name = it->path().filename().string();
                    if (name.size() > suffix.size() && name.ends_with(suffix))
                        names.push_back(name);
                }
                it.increment(ec);
            }
            std::sort(names.begin(), names.end());
            return names;
        }

        /**
         * @brief 日志滚动与文件保留策略管理状态
         */
        struct PurgeState
        {
            std::deque<std::string> knownFiles;
            bool baselineReady = false;
            std::string key;
            dev_t dirDev = 0;
            ino_t dirIno = 0;
        };

        /**
         * @brief 增量维护并淘汰超出 maxFiles 数量的最旧日志文件
         *
         * 采用 O(1) 增量维护队列：
         * 仅在首次滚动、目录变更（chdir）或 basename 变更时对当前工作目录进行一次基线扫描；
         * 后续滚动时执行二分插入与 pop_front 淘汰，极大减少持有滚动锁期间因大量文件 IO 遍历而引发的线程阻塞。
         *
         * @param state 滚动保留状态机
         * @param suffix 当前进程与当前 basename/tid 对应的后缀键
         * @param newFilename 刚刚生成创建的新日志文件名
         * @param maxFiles 最多允许保留的历史文件数（为 0 时表示不设限，不删除任何文件）
         */
        inline void purgeOldFiles(PurgeState &state, const std::string &suffix, std::string_view newFilename,
                                  size_t maxFiles)
        {
            if (maxFiles == 0)
            {
                state.baselineReady = false;
                state.knownFiles.clear();
                state.key.clear();
                return;
            }
            namespace fs = std::filesystem;
            struct stat dot{};
            const bool dirChanged =
                (::stat(".", &dot) != 0) || dot.st_dev != state.dirDev || dot.st_ino != state.dirIno;
            if (!state.baselineReady || state.key != suffix || dirChanged)
            {
                std::vector<std::string> names = scanCandidateFiles(suffix);
                state.knownFiles.assign(names.begin(), names.end());
                state.key = suffix;
                state.dirDev = dot.st_dev;
                state.dirIno = dot.st_ino;
                state.baselineReady = true;
            }
            if (!newFilename.empty())
            {
                const std::string name(newFilename);
                if (isRegularFile(name.c_str()))
                {
                    const auto pos = std::lower_bound(state.knownFiles.begin(), state.knownFiles.end(), name);
                    if (pos == state.knownFiles.end() || *pos != name)
                        state.knownFiles.insert(pos, name);
                }
            }
            while (state.knownFiles.size() > maxFiles)
            {
                std::error_code rmEc;
                fs::remove(state.knownFiles.front(), rmEc);
                state.knownFiles.pop_front();
            }
        }
    } // namespace detail

    /**
     * @brief 日志落盘文件管理器
     *
     * 支持两种落盘拓扑布局：
     * 1. 共享单文件（Shared Single File）：
     *    所有业务线程共享同一个日志文件。每个线程维护独立的 TLS 缓冲区，
     *    攒满后通过 Linux 内核针对 O_APPEND 文件的原子追加写入（write(2)）特性直接落盘。
     *    热路径无互斥锁争用、零二次拷贝；
     *    滚动时采用代次计数器（Generation Counter）与双检保护机制，保证文件安全切换。
     * 2. 每线程独立文件（Per-Thread File）：
     *    每个线程拥有独立归属的日志文件与私有 fd，完全无跨线程竞争，延迟稳定性极致；
     *    适合超高并发、极低延迟要求且长生命周期的工作线程场景。
     */
    class FileManager
    {
    public:
        /**
         * @brief 文件管理器运行配置
         */
        struct Config
        {
            size_t threadBufferCapacity = 64UL << 10;
            size_t fileBufferCapacity = 128UL << 10;
            size_t fileCapacity = 100UL << 20;
            size_t intervalFlushFile_ms = 30UL * 1000;
            std::string basename = "default";
            size_t maxFiles = 5;

            /**
             * @brief 校验并规范化配置各项阈值至安全范围内
             */
            void normalize() noexcept
            {
                fileBufferCapacity = std::min(std::max(fileBufferCapacity, detail::kMinFileBufferCapacity),
                                              detail::kMaxFileBufferCapacity);
                fileCapacity = std::min(std::max(fileCapacity, detail::kMinFileCapacity), detail::kMaxFileCapacity);
                intervalFlushFile_ms = std::min(intervalFlushFile_ms, detail::kMaxIntervalFlushMs);
                maxFiles = std::min(maxFiles, detail::kMaxMaxFiles);
                threadBufferCapacity =
                    std::min(std::min(threadBufferCapacity, detail::kMaxThreadBufferCapacity), fileCapacity);
                if (basename.empty())
                    basename = "default";
            }
        };

        /**
         * @brief 校验日志文件基名（basename）是否合法
         *
         * 允许字符集：[a-zA-Z0-9._-]，拒绝包含路径分隔符、空字符串及相对路径保留名（"."、".."）。
         *
         * @param name 待检测的文件基名视图
         * @return true 格式合法
         * @return false 包含非法字符或超出最大长度限制
         */
        static bool isValidBasename(std::string_view name) noexcept
        {
            if (name.empty() || name.size() > detail::kMaxBasenameLength)
                return false;
            for (char c : name)
            {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                c == '.' || c == '-' || c == '_';
                if (!ok)
                    return false;
            }
            return name != "." && name != "..";
        }

    private:
        std::atomic<int> fd_{-1};
        std::atomic<uint64_t> gen_{1};
        std::atomic<uint64_t> writers_{0};
        std::atomic<size_t> fileBytes_{0};
        std::atomic<bool> rollPending_{false};

        std::string sharedBuffer_;
        std::string openedBasename_;
        std::chrono::steady_clock::time_point lastFlushSteady_{std::chrono::steady_clock::now()};
        std::atomic<int64_t> lastFlushTicks_{0};
        std::mutex mtx_;

        detail::PurgeState purge_;
        ConfigHolder<Config> cfgHolder_;

        /**
         * @brief 获取当前线程私有的最新配置快照引用
         *
         * @return const Config& 当前生效配置的常量引用
         */
        const Config &refConfig() const noexcept
        {
            thread_local ConfigSnapshot<Config> snapshot(cfgHolder_);
            return snapshot.refConfig();
        }

        /**
         * @brief 更新最近一次刷盘时间戳（持有互斥锁）
         */
        void touchFlushTimeLocked() noexcept
        {
            lastFlushSteady_ = std::chrono::steady_clock::now();
            lastFlushTicks_.store(static_cast<int64_t>(lastFlushSteady_.time_since_epoch().count()),
                                  std::memory_order_relaxed);
        }

        /**
         * @brief 更新最近一次刷盘时间戳（无锁快速更新）
         */
        void touchFlushTimeNoLock() noexcept
        {
            const auto now = std::chrono::steady_clock::now();
            lastFlushTicks_.store(static_cast<int64_t>(now.time_since_epoch().count()), std::memory_order_relaxed);
        }

        /**
         * @brief 在互斥锁保护下执行共享单文件滚动与新文件创建
         *
         * 1. 递增代次计数器 gen_，并原子置换旧 fd_ 为 -1；
         * 2. 自旋等待正在执行上一代 write(2) 的并发写者排空（writers_ == 0）；
         * 3. 安全 close 旧描述符，按当前时间戳创建打开新文件；
         * 4. 重置文件字节统计与待滚动标志，并触发历史文件淘汰清理。
         */
        void rotateSingleFileLocked()
        {
            gen_.fetch_add(1, std::memory_order_seq_cst);
            const int oldFd = fd_.exchange(-1, std::memory_order_seq_cst);

            while (writers_.load(std::memory_order_seq_cst) != 0)
                std::this_thread::yield();

            if (oldFd >= 0)
                ::close(oldFd);

            const Config &cfg = refConfig();

            const std::string filename = detail::getFilenameSingle(cfg.basename.c_str());

            const int fd = detail::openAppendNoFollow(filename.c_str());
            if (fd >= 0)
            {
                fileBytes_.store(detail::getFileSize(fd), std::memory_order_relaxed);
                fd_.store(fd, std::memory_order_seq_cst);
            }
            else
            {
                fileBytes_.store(0, std::memory_order_relaxed);
            }
            openedBasename_ = cfg.basename;
            rollPending_.store(false, std::memory_order_release);
            touchFlushTimeLocked();
            detail::purgeOldFiles(purge_, detail::getSingleFileSuffix(cfg.basename), filename, cfg.maxFiles);
        }

        /// @brief 兼容性别名
        void newFileSingleLocked() { rotateSingleFileLocked(); }

        /**
         * @brief 检查共享单文件是否需要触发滚动
         *
         * @param pending 即将写入的字节量
         * @return true 需要滚动（文件未打开、基名改变、或当前大小加上待写入量超过 fileCapacity）
         * @return false 无需滚动
         */
        bool checkNeedRollSingleLocked(size_t pending) const
        {
            if (fd_.load(std::memory_order_relaxed) < 0)
                return true;
            const Config &cfg = refConfig();
            if (openedBasename_.empty() || cfg.basename != openedBasename_)
                return true;
            return fileBytes_.load(std::memory_order_relaxed) + sharedBuffer_.size() + pending > cfg.fileCapacity;
        }

        /// @brief 兼容性别名
        bool needRollSingleLocked(size_t pending) const { return checkNeedRollSingleLocked(pending); }

        /**
         * @brief 将全局共享缓冲区中的数据刷入文件描述符（持有互斥锁）
         */
        void flushSharedBufferLocked() noexcept
        {
            if (sharedBuffer_.empty())
                return;
            const int fd = fd_.load(std::memory_order_relaxed);
            fileBytes_.fetch_add(detail::writeFile(fd, sharedBuffer_), std::memory_order_relaxed);
            sharedBuffer_.clear();
            touchFlushTimeLocked();
        }

        /// @brief 兼容性别名
        void writeFileLocked() noexcept { flushSharedBufferLocked(); }

        /**
         * @brief 共享单文件模式下加锁追加写入数据
         *
         * 用于未启用线程缓冲（threadBufferCapacity == 0）时的直接输出路径。
         *
         * @param data 待写入的数据视图
         */
        void appendFileSingleLocked(std::string_view data)
        {
            const Config &cfg = refConfig();
            if (checkNeedRollSingleLocked(data.size()))
            {
                flushSharedBufferLocked();
                rotateSingleFileLocked();
            }
            sharedBuffer_.append(data.data(), data.size());

            const std::chrono::milliseconds interval(cfg.intervalFlushFile_ms);
            const bool timeUp = std::chrono::steady_clock::now() - lastFlushSteady_ > interval;
            if (sharedBuffer_.size() >= cfg.fileBufferCapacity || timeUp)
                flushSharedBufferLocked();
        }

        /**
         * @brief 共享单文件后端的线程局部缓冲区上下文
         */
        struct TlsSingle
        {
            std::string data;
            uint32_t counter = 0;
            int fd = -1;
            uint64_t gen = 0;
            size_t capacity = 64UL << 10;

            TlsSingle()
            {
                capacity = FileManager::getInstance().getConfig().threadBufferCapacity;
                if (capacity > 0)
                    data.reserve(capacity);
            }

            ~TlsSingle()
            {
                if (!data.empty())
                    FileManager::getInstance().flushTls(*this);
            }
        };

        /**
         * @brief 获取当前线程私有的 TlsSingle 单例引用
         */
        static TlsSingle &tlsSingle()
        {
            static thread_local TlsSingle buffer;
            return buffer;
        }

        /**
         * @brief 无锁判断是否已达到定时定期刷盘时间
         *
         * @return true 距离上次刷盘已超过 intervalFlushFile_ms
         * @return false 尚未到期
         */
        bool isFlushDue() const noexcept
        {
            const auto interval = std::chrono::milliseconds(refConfig().intervalFlushFile_ms);
            const auto last = std::chrono::steady_clock::time_point(
                std::chrono::steady_clock::duration(static_cast<std::chrono::steady_clock::duration::rep>(
                    lastFlushTicks_.load(std::memory_order_relaxed))));
            return std::chrono::steady_clock::now() - last > interval;
        }

        /// @brief 兼容性别名
        bool flushDue() const noexcept { return isFlushDue(); }

        /**
         * @brief 共享单文件快路径：完全无锁落盘
         *
         * 校验本线程缓存的 (fd, gen) 是否依然为当前最新代次；
         * 若有效，递增原子活跃写者数 writers_ 后直接执行 write(2) 系统调用。
         *
         * @param buffer 本线程局部单文件缓冲区
         * @param data 待写盘数据块
         * @return true 数据已成功交给操作系统内核
         * @return false 遇到文件滚动或代次失效，需转走慢路径
         */
        bool writeSingleNoLock(TlsSingle &buffer, std::string_view data) noexcept
        {
            if (rollPending_.load(std::memory_order_acquire))
                return false;
            const uint64_t g = gen_.load(std::memory_order_seq_cst);
            if (buffer.fd < 0 || buffer.gen != g)
                return false;

            writers_.fetch_add(1, std::memory_order_seq_cst);
            if (gen_.load(std::memory_order_seq_cst) != g)
            {
                writers_.fetch_sub(1, std::memory_order_seq_cst);
                return false;
            }
            const size_t written = detail::writeFile(buffer.fd, data);
            writers_.fetch_sub(1, std::memory_order_seq_cst);

            if (written == data.size())
            {
                fileBytes_.fetch_add(written, std::memory_order_relaxed);
                const Config &cfg = refConfig();
                if (fileBytes_.load(std::memory_order_relaxed) >= cfg.fileCapacity)
                    rollPending_.store(true, std::memory_order_release);
                touchFlushTimeNoLock();
            }
            else
            {
                rollPending_.store(true, std::memory_order_release);
            }
            return true;
        }

        /**
         * @brief 共享单文件慢路径：加锁同步描述符并落盘
         *
         * 获取 mtx_ 锁，检测并按需执行 rotateSingleFileLocked()，刷新本线程缓存的 (fd, gen) 并完成写盘。
         *
         * @param buffer 本线程局部单文件缓冲区
         * @param data 待写盘数据字符串引用（落盘后清空）
         */
        void flushTlsSlow(TlsSingle &buffer, std::string &data) noexcept
        {
            std::lock_guard<std::mutex> locker(mtx_);
            const Config &cfg = refConfig();
            if (fd_.load(std::memory_order_relaxed) < 0 || openedBasename_.empty() || cfg.basename != openedBasename_ ||
                fileBytes_.load(std::memory_order_relaxed) + data.size() > cfg.fileCapacity)
            {
                rotateSingleFileLocked();
            }
            else
            {
                rollPending_.store(false, std::memory_order_release);
            }
            buffer.fd = fd_.load(std::memory_order_relaxed);
            buffer.gen = gen_.load(std::memory_order_relaxed);
            const size_t written = detail::writeFile(buffer.fd, data);
            fileBytes_.fetch_add(written, std::memory_order_relaxed);
            if (written == data.size())
            {
                touchFlushTimeLocked();
                if (fileBytes_.load(std::memory_order_relaxed) >= cfg.fileCapacity)
                    rollPending_.store(true, std::memory_order_release);
            }
            else
            {
                rollPending_.store(true, std::memory_order_release);
            }
            data.clear();
        }

        /**
         * @brief 刷新线程私有单文件缓冲区
         *
         * 优先尝试无锁快路径，失败时转入慢路径安全落盘。
         *
         * @param buffer 本线程单文件缓冲区对象
         */
        void flushTls(TlsSingle &buffer) noexcept
        {
            if (buffer.data.empty())
                return;
            if (writeSingleNoLock(buffer, buffer.data))
            {
                buffer.data.clear();
                return;
            }
            flushTlsSlow(buffer, buffer.data);
        }

        /**
         * @brief 每线程独立文件后端的线程局部上下文
         */
        struct TlsMultiple
        {
            std::string data;
            uint32_t counter = 0;
            int fd = -1;
            size_t bytes = 0;
            std::string basename;
            detail::PurgeState purge;
            std::chrono::steady_clock::time_point lastFlush{std::chrono::steady_clock::now()};
            size_t capacity = 64UL << 10;

            TlsMultiple()
            {
                capacity = FileManager::getInstance().getConfig().threadBufferCapacity;
                if (capacity > 0)
                    data.reserve(capacity);
            }

            ~TlsMultiple()
            {
                FileManager &fm = FileManager::getInstance();
                if (!data.empty())
                    fm.writeMultipleFile(*this, data);
                fm.closeMultipleFile(*this);
            }
        };

        /**
         * @brief 获取当前线程私有的 TlsMultiple 单例引用
         */
        static TlsMultiple &tlsMultiple()
        {
            static thread_local TlsMultiple buffer;
            return buffer;
        }

        /**
         * @brief 打开或滚动到本线程独立的日志文件
         *
         * 仅被持有该 TlsMultiple 的归属线程调用，完全无需加锁同步。
         *
         * @param buffer 目标线程的多文件上下文
         */
        void openMultipleFile(TlsMultiple &buffer) noexcept
        {
            const Config &cfg = refConfig();
            const long long tid = detail::getThreadId();
            const std::string filename = detail::getFilenameMultiple(cfg.basename.c_str(), tid);
            const int fd = detail::openAppendNoFollow(filename.c_str());
            if (fd >= 0)
            {
                buffer.bytes = detail::getFileSize(fd);
                buffer.fd = fd;
            }
            else
            {
                buffer.bytes = 0;
                buffer.fd = -1;
            }
            buffer.basename = cfg.basename;
            buffer.lastFlush = std::chrono::steady_clock::now();
            detail::purgeOldFiles(buffer.purge, detail::getMultipleFileSuffix(cfg.basename, tid), filename,
                                  cfg.maxFiles);
        }

        /**
         * @brief 写入本线程私有的独立日志文件
         *
         * 检查容量上限，超限时先关闭旧文件再开辟新文件，然后执行写入。
         *
         * @param buffer 目标线程的多文件上下文
         * @param data 待写入的数据视图
         */
        void writeMultipleFile(TlsMultiple &buffer, std::string_view data) noexcept
        {
            const Config &cfg = refConfig();
            const bool basenameChanged = buffer.basename.empty() || (cfg.basename != buffer.basename);
            if (buffer.fd < 0 || basenameChanged || buffer.bytes + data.size() > cfg.fileCapacity)
            {
                closeMultipleFile(buffer);
                openMultipleFile(buffer);
            }
            if (data.empty())
                return;
            buffer.bytes += detail::writeFile(buffer.fd, data);
            buffer.lastFlush = std::chrono::steady_clock::now();
        }

        /**
         * @brief 关闭本线程私有的独立日志文件描述符
         *
         * @param buffer 目标线程的多文件上下文
         */
        void closeMultipleFile(TlsMultiple &buffer) noexcept
        {
            if (buffer.fd >= 0)
            {
                ::close(buffer.fd);
                buffer.fd = -1;
            }
            buffer.bytes = 0;
            buffer.basename.clear();
        }

        /**
         * @brief 刷新本线程私有的多文件缓冲区至磁盘
         *
         * @param buffer 目标线程的多文件上下文
         */
        void flushMultiple(TlsMultiple &buffer) noexcept
        {
            if (buffer.data.empty())
                return;
            writeMultipleFile(buffer, buffer.data);
            buffer.data.clear();
        }

        FileManager()
        {
            std::lock_guard<std::mutex> locker(mtx_);
            sharedBuffer_.reserve(refConfig().fileBufferCapacity);
            touchFlushTimeLocked();
        }
        ~FileManager() = default;
        FileManager(const FileManager &) = delete;
        FileManager &operator=(const FileManager &) = delete;
        FileManager(FileManager &&) noexcept = delete;
        FileManager &operator=(FileManager &&) noexcept = delete;

    public:
        /**
         * @brief 获取当前文件管理器的配置快照
         *
         * @return Config 当前生效的配置对象
         */
        Config getConfig() const noexcept { return refConfig(); }

        /**
         * @brief 更新并发布文件管理器配置
         *
         * @param cfg 新的配置对象（自动进行规范化处理）
         */
        void setConfig(Config cfg)
        {
            cfg.normalize();
            cfgHolder_.publish(std::move(cfg));
        }

        /**
         * @brief 获取 FileManager 全局单例
         *
         * @return FileManager& 单例对象引用
         */
        inline static FileManager &getInstance() noexcept
        {
            static FileManager *instance = new FileManager();
            return *instance;
        }

        /**
         * @brief 共享单文件后端写入日志消息
         *
         * 将消息追加至当前线程的 TLS 缓冲区；当缓冲区写满或定时周期达标时，触发刷盘。
         *
         * @param msg 格式化完成的单条日志数据
         */
        void appendFile(std::string_view msg)
        {
            if (msg.empty()) [[unlikely]]
                return;

            TlsSingle &buffer = tlsSingle();
            if (buffer.capacity == 0) [[unlikely]]
            {
                std::lock_guard<std::mutex> locker(mtx_);
                appendFileSingleLocked(msg);
                return;
            }

            buffer.data.append(msg.data(), msg.size());

            if (buffer.data.size() >= buffer.capacity) [[unlikely]]
            {
                flushTls(buffer);
                buffer.counter = 0;
                buffer.capacity = refConfig().threadBufferCapacity;
            }
            else if ((++buffer.counter & 0xFFu) == 0 && isFlushDue()) [[unlikely]]
            {
                flushTls(buffer);
                buffer.counter = 0;
            }
        }

        /**
         * @brief 强制将当前线程和全局共享缓冲区中的数据刷入共享日志文件
         */
        void flushFile()
        {
            TlsSingle &buffer = tlsSingle();
            std::lock_guard<std::mutex> locker(mtx_);
            const Config &cfg = refConfig();
            if (fd_.load(std::memory_order_relaxed) < 0 || openedBasename_.empty() || cfg.basename != openedBasename_)
            {
                flushSharedBufferLocked();
                rotateSingleFileLocked();
            }
            if (!buffer.data.empty())
            {
                appendFileSingleLocked(buffer.data);
                buffer.data.clear();
            }
            flushSharedBufferLocked();
            buffer.fd = fd_.load(std::memory_order_relaxed);
            buffer.gen = gen_.load(std::memory_order_relaxed);
            buffer.counter = 0;
            rollPending_.store(false, std::memory_order_release);
        }

        /**
         * @brief 每线程独立文件后端写入日志消息
         *
         * 将消息写入本线程归属的独立文件缓冲区，并在容量满或定时到期时刷入该线程独占的日志文件。
         *
         * @param msg 格式化完成的单条日志数据
         */
        void appendFilePerThread(std::string_view msg)
        {
            if (msg.empty()) [[unlikely]]
                return;
            TlsMultiple &buffer = tlsMultiple();
            if (buffer.capacity == 0) [[unlikely]]
            {
                writeMultipleFile(buffer, msg);
                return;
            }

            buffer.data.append(msg.data(), msg.size());
            if (buffer.data.size() >= buffer.capacity) [[unlikely]]
            {
                flushMultiple(buffer);
                buffer.counter = 0;
                buffer.capacity = refConfig().threadBufferCapacity;
            }
            else if ((++buffer.counter & 0xFFu) == 0) [[unlikely]]
            {
                const std::chrono::milliseconds interval(refConfig().intervalFlushFile_ms);
                if (std::chrono::steady_clock::now() - buffer.lastFlush > interval)
                {
                    flushMultiple(buffer);
                    buffer.counter = 0;
                }
            }
        }

        /**
         * @brief 强制将当前线程的独立文件缓冲区刷入其独占日志文件
         */
        void flushFilePerThread()
        {
            TlsMultiple &buffer = tlsMultiple();
            writeMultipleFile(buffer, buffer.data);
            buffer.data.clear();
            buffer.counter = 0;
        }

        inline static void fileOutputThunk(void * /*ctx*/, std::string_view msg) { getInstance().appendFile(msg); }
        inline static void fileFlushThunk(void * /*ctx*/) { getInstance().flushFile(); }

        /**
         * @brief 借出共享单文件线程缓冲区供 Logger 直接格式化填充（实现零拷贝）
         *
         * @return std::string* 线程局部缓冲字符串指针；若未启用缓冲则返回 nullptr
         */
        inline static std::string *acquireSingleBuffer(void * /*ctx*/) noexcept
        {
            TlsSingle &buffer = tlsSingle();
            if (buffer.capacity == 0) [[unlikely]]
                return nullptr;
            return &buffer.data;
        }

        /**
         * @brief 归还共享单文件线程缓冲区并在达标时触发自动刷盘
         *
         * @param written 本次写入的字节长度
         */
        inline static void releaseSingleBuffer(void * /*ctx*/, size_t /*written*/) noexcept
        {
            TlsSingle &buffer = tlsSingle();
            if (buffer.data.size() >= buffer.capacity) [[unlikely]]
            {
                getInstance().flushTls(buffer);
                buffer.counter = 0;
                buffer.capacity = getInstance().refConfig().threadBufferCapacity;
            }
            else if ((++buffer.counter & 0xFFu) == 0 && getInstance().isFlushDue()) [[unlikely]]
            {
                getInstance().flushTls(buffer);
                buffer.counter = 0;
            }
        }

        /**
         * @brief 创建绑定至共享单文件后端的输出函数适配结构体
         *
         * @return OutputFunction 输出函数对象
         */
        inline static OutputFunction outputFunction_file() noexcept
        {
            return OutputFunction{&fileOutputThunk, nullptr, &acquireSingleBuffer, &releaseSingleBuffer};
        }

        /**
         * @brief 创建绑定至共享单文件后端的刷新函数适配结构体
         *
         * @return FlushFunction 刷新函数对象
         */
        inline static FlushFunction flushFunction_file() noexcept { return FlushFunction{&fileFlushThunk, nullptr}; }

        /// @brief 动词+名词 规范命名的工厂别名
        inline static OutputFunction createFileOutputFunction() noexcept { return outputFunction_file(); }

        /// @brief 动词+名词 规范命名的工厂别名
        inline static FlushFunction createFileFlushFunction() noexcept { return flushFunction_file(); }

        inline static void perThreadOutputThunk(void * /*ctx*/, std::string_view msg)
        {
            getInstance().appendFilePerThread(msg);
        }
        inline static void perThreadFlushThunk(void * /*ctx*/) { getInstance().flushFilePerThread(); }

        /**
         * @brief 借出每线程独立文件缓冲区供 Logger 直接格式化填充（实现零拷贝）
         *
         * @return std::string* 线程局部缓冲字符串指针；若未启用缓冲则返回 nullptr
         */
        inline static std::string *acquireMultipleBuffer(void * /*ctx*/) noexcept
        {
            TlsMultiple &buffer = tlsMultiple();
            if (buffer.capacity == 0) [[unlikely]]
                return nullptr;
            return &buffer.data;
        }

        /**
         * @brief 归还每线程独立文件缓冲区并在达标时触发自动刷盘
         *
         * @param written 本次写入的字节长度
         */
        inline static void releaseMultipleBuffer(void * /*ctx*/, size_t /*written*/) noexcept
        {
            TlsMultiple &buffer = tlsMultiple();
            if (buffer.data.size() >= buffer.capacity) [[unlikely]]
            {
                getInstance().flushMultiple(buffer);
                buffer.counter = 0;
                buffer.capacity = getInstance().refConfig().threadBufferCapacity;
            }
            else if ((++buffer.counter & 0xFFu) == 0) [[unlikely]]
            {
                const std::chrono::milliseconds interval(getInstance().refConfig().intervalFlushFile_ms);
                if (std::chrono::steady_clock::now() - buffer.lastFlush > interval)
                {
                    getInstance().flushMultiple(buffer);
                    buffer.counter = 0;
                }
            }
        }

        /**
         * @brief 创建绑定至每线程独立文件后端的输出函数适配结构体
         *
         * @return OutputFunction 输出函数对象
         */
        inline static OutputFunction outputFunction_filePerThread() noexcept
        {
            return OutputFunction{&perThreadOutputThunk, nullptr, &acquireMultipleBuffer, &releaseMultipleBuffer};
        }

        /**
         * @brief 创建绑定至每线程独立文件后端的刷新函数适配结构体
         *
         * @return FlushFunction 刷新函数对象
         */
        inline static FlushFunction flushFunction_filePerThread() noexcept
        {
            return FlushFunction{&perThreadFlushThunk, nullptr};
        }

        /// @brief 动词+名词 规范命名的工厂别名
        inline static OutputFunction createPerThreadFileOutputFunction() noexcept
        {
            return outputFunction_filePerThread();
        }

        /// @brief 动词+名词 规范命名的工厂别名
        inline static FlushFunction createPerThreadFileFlushFunction() noexcept
        {
            return flushFunction_filePerThread();
        }
    };

} // namespace mylog
