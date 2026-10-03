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
        inline constexpr size_t kMinFileBufferCapacity = 1024;
        inline constexpr size_t kMaxFileBufferCapacity = 256 * 1024 * 1024;
        inline constexpr size_t kMaxThreadBufferCapacity = 64 * 1024 * 1024;
        inline constexpr size_t kMinFileCapacity = 1024;
        inline constexpr size_t kMaxFileCapacity = 64ULL * 1024 * 1024 * 1024;
        inline constexpr size_t kMaxIntervalFlushMs = 24ULL * 60 * 60 * 1000;
        inline constexpr size_t kMaxBasenameLength = 128;
        inline constexpr size_t kMaxMaxFiles = 1024;
    } // namespace detail

} // namespace mylog

#include "configHolder.hpp"
#include "logger.hpp"

namespace mylog
{

    namespace detail
    {
        inline std::string getTimestampString()
        {
            const auto now = std::chrono::system_clock::now();
            const auto secs = std::chrono::floor<std::chrono::seconds>(now);
            const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now - secs);
            return std::format("{:%Y%m%d_%H%M%S}_{:09}", secs, nanos.count());
        }

        inline void writeFallback(std::string_view data) noexcept
        {
            if (data.empty())
                return;
            std::fwrite(data.data(), 1, data.size(), stdout);
            std::fflush(stdout);
        }

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

        inline int openAppendNoFollow(const char *path) noexcept
        {
            return ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0644);
        }

        inline size_t fileSizeOf(int fd) noexcept
        {
            struct stat st{};
            return (::fstat(fd, &st) == 0) ? static_cast<size_t>(st.st_size) : 0;
        }

        inline bool isRegularFile(const char *path) noexcept
        {
            struct stat st{};
            return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
        }

        inline std::string getFilenameSingle(const char *basename)
        {
            return std::format("{}_{}_{}.log", getTimestampString(), basename,
                               static_cast<long long>(::getpid()));
        }

        inline std::string singleSuffix(std::string_view basename)
        {
            return "_" + std::string(basename) + "_" +
                   std::to_string(static_cast<long long>(::getpid())) + ".log";
        }

        inline std::string getFilenameMultiple(const char *basename, long long tid)
        {
            return std::format("{}_{}_{}_{}.log", getTimestampString(), basename,
                               static_cast<long long>(::getpid()), tid);
        }

        inline std::string multipleSuffix(std::string_view basename, long long tid)
        {
            return "_" + std::string(basename) + "_" +
                   std::to_string(static_cast<long long>(::getpid())) + "_" +
                   std::to_string(tid) + ".log";
        }

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

        struct PurgeState
        {
            std::deque<std::string> knownFiles;
            bool baselineReady = false;
            std::string key;
            dev_t dirDev = 0;
            ino_t dirIno = 0;
        };

        // 保留策略：删除超出 maxFiles 的最旧日志文件（只处理当前进程的文件，避免跨进程误删）。
        //
        // 增量维护（改造点）：原实现每次滚动都用 directory_iterator(".") 全量遍历当前目录，
        // 而滚动发生在持有 mtx_ 的 newFileSingleLocked() 里，目录很大时（共享日志目录、$HOME 下）
        // 会阻塞所有写日志的线程。现在只在第一次滚动（或 basename 变化、目录变化、maxFiles
        // 从 0 变回非 0）时扫描一次建立基线 knownFiles，之后每次滚动只做 O(1) 的插入 + 裁剪。
        //
        // 正确性论证：候选集合只可能因为本进程自己的滚动而增加（文件名带本进程 pid，其它进程
        // 不会写出同后缀的文件），所以「首扫得到的集合 + 之后本进程滚出的新文件名」恒等于原实现
        // 在那一刻全量扫描得到的集合；再按同样的「字典序最小的先删」规则裁剪，结果逐次一致。
        // 首扫会看到进程启动前遗留的文件（含 pid 复用导致的上一次运行残留、以及同一个 pid 但更早
        // basename 的文件），因此不会漏记、更不会漏删。
        //
        // 边界与取舍：
        //   * 新文件名按有序插入（正常情况就是尾部；只有系统时钟回拨才可能落到中间），
        //     保证 pop_front() 删除的始终是字典序最小 = 最旧的文件，而不是「最近插入的」。
        //   * 同一文件名只记一次（极端情况下同一纳秒重复滚动会打开同一个文件，重复计数会多删）。
        //   * 删除失败（权限、竞态、外部清理脚本已删掉该文件）只忽略，与原实现一致，不重建基线
        //     —— 重建会退化成每次滚动都扫描。代价是这种情况下保留数可能暂时少于 maxFiles。
        //   * 运行期 chdir：基线绑定的是建立它的那个目录（dev+ino），每次滚动用一次 stat(".")
        //     校验，目录换了就重新全量扫描。因此不会拿着 A 目录的文件名去删 B 目录里的东西。
        //     （stat 失败时按「已变化」处理，退化成每次扫描，不会误删。）
        //   * 外部因素新建的同 pid + 同 basename 文件（例如多台主机共享目录且 pid 恰好相同、
        //     或用户手工拷入旧日志）不会进入基线，直到 basename 变化或 maxFiles 归零后
        //     重新建基线；此期间只会「少删」，绝不会误删不属于本进程/本 basename 的文件。
        inline void purgeOldFiles(PurgeState &state, const std::string &suffix,
                                  std::string_view newFilename, size_t maxFiles)
        {
            if (maxFiles == 0)
            {
                // 不限制数量：清掉基线。否则 maxFiles 之后再被改成非 0 时，会把「不限制」期间
                // 滚出的文件漏掉而少删；下次非 0 滚动重新全量扫描一次即可恢复精确。
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
                // 首次滚动 / basename 变化 / 目录变化：全量扫描建基线（这是唯一的目录遍历点）
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
                // 打开失败时文件可能并不存在，凭空记一个名字会多删一个真实文件
                if (isRegularFile(name.c_str()))
                {
                    const auto pos =
                        std::lower_bound(state.knownFiles.begin(), state.knownFiles.end(), name);
                    if (pos == state.knownFiles.end() || *pos != name)
                        state.knownFiles.insert(pos, name);
                }
            }
            while (state.knownFiles.size() > maxFiles)
            {
                std::error_code rmEc;
                fs::remove(state.knownFiles.front(), rmEc); // 尽力而为，忽略删除失败
                state.knownFiles.pop_front();
            }
        }
    } // namespace detail

    // 落盘布局：
    //
    //      共享单文件：每个业务线程各自攒线程局部缓冲，攒满后把整块直接 write(2) 到
    //      同一个 O_APPEND fd。追加的原子性由内核的 file description 保证（O_APPEND 下
    //      write 的 offset 定位与写入是同一次原子操作），所以用户态热路径零锁、零二次拷贝。
    //      代价是滚动（换文件）时存在一个很短的"静默窗口"，见 newFileSingleLocked()。
    //
    //      每线程独立文件：每个线程写自己的文件，线程之间不共享任何写状态（连 fd 都不共享），
    //      因此没有任何跨线程竞争，业务线程尾延迟最稳。代价是文件数 = 活跃线程数 × maxFiles，
    //      线程数远大于核数、或线程生命周期短时会产生大量小文件。
    class FileManager
    {
    public:
        struct Config
        {
            size_t threadBufferCapacity = 65536UL; 
            size_t fileBufferCapacity = 131072UL;
            size_t fileCapacity = 104857600UL;
            size_t intervalFlushFile_ms = 30000UL;
            std::string basename = "default";
            size_t maxFiles = 5;

            void normalize() noexcept
            {
                fileBufferCapacity =
                    std::min(std::max(fileBufferCapacity, detail::kMinFileBufferCapacity),
                             detail::kMaxFileBufferCapacity);
                fileCapacity = std::min(std::max(fileCapacity, detail::kMinFileCapacity),
                                        detail::kMaxFileCapacity);
                intervalFlushFile_ms = std::min(intervalFlushFile_ms, detail::kMaxIntervalFlushMs);
                maxFiles = std::min(maxFiles, detail::kMaxMaxFiles);
                threadBufferCapacity = std::min(
                    std::min(threadBufferCapacity, detail::kMaxThreadBufferCapacity), fileCapacity);
                if (basename.empty())
                    basename = "default";
            }
        };

        static bool isValidBasename(std::string_view name) noexcept
        {
            if (name.empty() || name.size() > detail::kMaxBasenameLength)
                return false;
            for (char c : name)
            {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
                if (!ok)
                    return false;
            }
            return name != "." && name != "..";
        }

    private:
        // fd_ / gen_ / writers_ 三者共同保证「滚动时不会把数据写进刚被复用的 fd 号」：
        //   写者：  gen 读 → writers_++ → 复核 gen 未变 → write(2) → writers_--
        //   滚动者：gen_++ → fd_ 置 -1 → 等 writers_ == 0 → close(旧 fd) → open(新文件)
        // 复核失败说明滚动已经开始，写者放弃快路径、转走慢路径重新同步 fd。
        std::atomic<int> fd_{-1};              // 当前共享文件 fd，-1 = 未打开（降级到 stdout）
        std::atomic<uint64_t> gen_{1};         // fd 的代次；每次滚动递增，使线程缓存的 fd 失效
        std::atomic<uint64_t> writers_{0};     // 正在用上一代 fd 做 write(2) 的写者数
        std::atomic<size_t> fileBytes_{0};     // 已落盘字节数（各线程并发累加，近似值）
        std::atomic<bool> rollPending_{false}; // 已达容量上限，下一个落盘的线程负责滚动

        std::string sharedBuffer_;   // 待写盘数据，受 mtx_ 保护（仅 threadBufferCapacity==0 时使用）
        std::string openedBasename_; // 当前文件名对应的 basename
        std::chrono::steady_clock::time_point lastFlushSteady_{std::chrono::steady_clock::now()};
        std::atomic<int64_t> lastFlushTicks_{0}; // 与 lastFlushSteady_ 同步，供无锁路径读取
        std::mutex mtx_;                         // 只保护滚动与 sharedBuffer_，热路径不取

        detail::PurgeState purge_;       // 保留策略基线，只被持 mtx_ 的滚动路径访问
        ConfigHolder<Config> cfgHolder_; // 配置快照，由本单例自己持有（热路径无锁读）

        const Config &refConfig() const noexcept
        {
            thread_local ConfigSnapshot<Config> snapshot(cfgHolder_);
            return snapshot.refConfig();
        }

        void touchFlushTimeLocked() noexcept
        {
            lastFlushSteady_ = std::chrono::steady_clock::now();
            lastFlushTicks_.store(
                static_cast<int64_t>(lastFlushSteady_.time_since_epoch().count()),
                std::memory_order_relaxed);
        }

        void touchFlushTimeNoLock() noexcept
        {
            const auto now = std::chrono::steady_clock::now();
            lastFlushTicks_.store(static_cast<int64_t>(now.time_since_epoch().count()),
                                  std::memory_order_relaxed);
        }

        void newFileSingleLocked()
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
                fileBytes_.store(detail::fileSizeOf(fd), std::memory_order_relaxed);
                fd_.store(fd, std::memory_order_seq_cst);
            }
            else
            {
                fileBytes_.store(0, std::memory_order_relaxed);
            }
            openedBasename_ = cfg.basename;
            rollPending_.store(false, std::memory_order_release);
            touchFlushTimeLocked();
            detail::purgeOldFiles(purge_, detail::singleSuffix(cfg.basename), filename, cfg.maxFiles);
        }

        bool needRollSingleLocked(size_t pending) const
        {
            if (fd_.load(std::memory_order_relaxed) < 0)
                return true; 
            const Config &cfg = refConfig();
            if (openedBasename_.empty() || cfg.basename != openedBasename_)
                return true;
            return fileBytes_.load(std::memory_order_relaxed) + sharedBuffer_.size() + pending >
                   cfg.fileCapacity;
        }

        void writeFileLocked() noexcept
        {
            if (sharedBuffer_.empty())
                return;
            const int fd = fd_.load(std::memory_order_relaxed);
            fileBytes_.fetch_add(detail::writeFile(fd, sharedBuffer_), std::memory_order_relaxed);
            sharedBuffer_.clear();
            touchFlushTimeLocked();
        }

        void appendFileSingleLocked(std::string_view data)
        {
            const Config &cfg = refConfig();
            if (needRollSingleLocked(data.size()))
            {
                writeFileLocked();
                newFileSingleLocked();
            }
            sharedBuffer_.append(data.data(), data.size());

            const std::chrono::milliseconds interval(cfg.intervalFlushFile_ms);
            const bool timeUp = std::chrono::steady_clock::now() - lastFlushSteady_ > interval;
            if (sharedBuffer_.size() >= cfg.fileBufferCapacity || timeUp)
                writeFileLocked();
        }

        struct TlsSingle
        {
            std::string data;
            uint32_t counter = 0;
            int fd = -1;      // 本线程缓存的共享 fd，与缓存的 gen 配对使用
            uint64_t gen = 0; // 缓存对应的文件代次

            ~TlsSingle()
            {
                // 线程退出时把剩余数据交给单例（单例永不析构，见 getInstance）
                if (!data.empty())
                    FileManager::getInstance().flushTls(*this);
            }
        };

        static TlsSingle &tlsSingle()
        {
            static thread_local TlsSingle buffer;
            return buffer;
        }

        // 无锁判断是否到了 periodic flush 的时间
        bool flushDue() const noexcept
        {
            const auto interval = std::chrono::milliseconds(
                refConfig().intervalFlushFile_ms);
            const auto last = std::chrono::steady_clock::time_point(
                std::chrono::steady_clock::duration(
                    static_cast<std::chrono::steady_clock::duration::rep>(
                        lastFlushTicks_.load(std::memory_order_relaxed))));
            return std::chrono::steady_clock::now() - last > interval;
        }

        // 快路径：完全不取锁，只做「确认字 + 引用计数 + 一次 write(2)」。
        // 返回 true 表示数据已经交给内核（或降级给了 stdout），调用者可以清空缓冲。
        bool writeSingleNoLock(TlsSingle &buffer, std::string_view data) noexcept
        {
            if (rollPending_.load(std::memory_order_acquire))
                return false; // 该滚动文件了，交给慢路径统一处理
            const uint64_t g = gen_.load(std::memory_order_seq_cst);
            if (buffer.fd < 0 || buffer.gen != g)
                return false; // 本线程还没同步到当前文件

            // 进入临界区。seq_cst 保证下面的复核与滚动线程的 gen_ 递增有全局顺序，
            // 从而「滚动线程看到 writers_ == 0」必然意味着本次 write 已经结束。
            writers_.fetch_add(1, std::memory_order_seq_cst);
            if (gen_.load(std::memory_order_seq_cst) != g)
            {
                writers_.fetch_sub(1, std::memory_order_seq_cst);
                return false; // 滚动已经开始，本线程的 fd 已经过代
            }
            const size_t written = detail::writeFile(buffer.fd, data);
            writers_.fetch_sub(1, std::memory_order_seq_cst);

            if (written == data.size())
            {
                fileBytes_.fetch_add(written, std::memory_order_relaxed);
                const Config &cfg = refConfig();
                if (fileBytes_.load(std::memory_order_relaxed) >= cfg.fileCapacity)
                    rollPending_.store(true, std::memory_order_release);
                touchFlushTimeNoLock(); // 数据刚进内核，把 periodic flush 的计时基准推到现在
            }
            else
            {
                rollPending_.store(true, std::memory_order_release); // 写残了，让下一个落盘的线程重开文件
            }
            return true;
        }

        // 慢路径：取滚动锁，同步本线程缓存的 fd，然后整块写出。
        void flushTlsSlow(TlsSingle &buffer, std::string &data) noexcept
        {
            std::lock_guard<std::mutex> locker(mtx_);
            const Config &cfg = refConfig();
            if (fd_.load(std::memory_order_relaxed) < 0 || openedBasename_.empty() ||
                cfg.basename != openedBasename_ ||
                fileBytes_.load(std::memory_order_relaxed) + data.size() > cfg.fileCapacity)
            {
                newFileSingleLocked();
            }
            else
            {
                // rollPending_ 只是一个提示（例如因写残而置位，或 fileCapacity 已被配置改大）
                // 在真正复核过容量之后消费掉它，否则快路径会永久失效。
                rollPending_.store(false, std::memory_order_release);
            }
            // 同步本线程的 (fd, gen) 缓存；此后若不再滚动就可一直走快路径
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

        struct TlsMultiple
        {
            std::string data;
            uint32_t counter = 0;
            int fd = -1;
            size_t bytes = 0;         // 本线程当前文件已落盘字节数
            std::string basename;     // 本线程当前文件对应的 basename
            detail::PurgeState purge; // 本线程私有的保留基线，不需要锁
            std::chrono::steady_clock::time_point lastFlush{std::chrono::steady_clock::now()};

            ~TlsMultiple()
            {
                FileManager &fm = FileManager::getInstance();
                if (!data.empty())
                    fm.writeMultipleFile(*this, data); // 线程退出时把剩余数据写进自己的文件
                fm.closeMultipleFile(*this);
            }
        };

        static TlsMultiple &tlsMultiple()
        {
            static thread_local TlsMultiple buffer;
            return buffer;
        }

        // 打开（或滚动到）本线程自己的文件。只被本线程调用，因此无需任何同步。
        void openMultipleFile(TlsMultiple &buffer) noexcept
        {
            const Config &cfg = refConfig();
            const long long tid = detail::threadId();
            // 新文件名使用当前时间
            const std::string filename = detail::getFilenameMultiple(cfg.basename.c_str(), tid);
            const int fd = detail::openAppendNoFollow(filename.c_str());
            if (fd >= 0)
            {
                buffer.bytes = detail::fileSizeOf(fd);
                buffer.fd = fd;
            }
            else
            {
                buffer.bytes = 0;
                buffer.fd = -1; // 降级到 stdout
            }
            buffer.basename = cfg.basename;
            buffer.lastFlush = std::chrono::steady_clock::now();
            detail::purgeOldFiles(buffer.purge, detail::multipleSuffix(cfg.basename, tid), filename, cfg.maxFiles);
        }

        // 写入本线程自己的文件。data 不跨文件：容量到顶时先关旧文件再开新的。
        void writeMultipleFile(TlsMultiple &buffer, std::string_view data) noexcept
        {
            const Config &cfg = refConfig();
            const bool basenameChanged =
                buffer.basename.empty() || (cfg.basename != buffer.basename);
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
            // 有意不在这里 newFileSingleLocked()：共享单文件布局（A）虽然是默认布局，但只用
            // 每线程独立文件布局（A2）的进程不该被凭空造出一个 0 字节的共享文件。
            // A 的三条入口（appendFileSingleLocked / flushTlsSlow / flushFile）在
            // fd_ < 0 时都会自己 newFileSingleLocked()，所以惰性打开不会漏掉任何数据。
        }
        ~FileManager() = default;
        FileManager(const FileManager &) = delete;
        FileManager &operator=(const FileManager &) = delete;
        FileManager(FileManager &&) noexcept = delete;
        FileManager &operator=(FileManager &&) noexcept = delete;

    public:
        Config getConfig() const noexcept
        {
            return refConfig();
        }

        void setConfig(Config cfg)
        {
            cfg.normalize();
            cfgHolder_.publish(std::move(cfg));
        }

        inline static FileManager &getInstance() noexcept
        {
            static FileManager *instance = new FileManager();
            return *instance;
        }

        void appendFile(std::string_view msg)
        {
            if (msg.empty())
                return;

            const Config &cfg = refConfig();

            const size_t tlsCapacity = cfg.threadBufferCapacity;
            if (tlsCapacity == 0)
            {
                std::lock_guard<std::mutex> locker(mtx_);
                appendFileSingleLocked(msg);
                return;
            }

            TlsSingle &buffer = tlsSingle();
            buffer.data.append(msg.data(), msg.size());

            if (buffer.data.size() >= tlsCapacity)
            {
                flushTls(buffer);
                buffer.counter = 0;
            }
            else if ((++buffer.counter & 0xFFu) == 0 && flushDue())
            {
                flushTls(buffer);
                buffer.counter = 0;
            }
        }

        void flushFile()
        {
            TlsSingle &buffer = tlsSingle();
            std::lock_guard<std::mutex> locker(mtx_);
            const Config &cfg = refConfig();
            // 冷启动 / 换了 basename：先把已有数据落在旧文件尾部，再切到新文件
            if (fd_.load(std::memory_order_relaxed) < 0 || openedBasename_.empty() ||
                cfg.basename != openedBasename_)
            {
                writeFileLocked();
                newFileSingleLocked();
            }
            if (!buffer.data.empty())
            {
                appendFileSingleLocked(buffer.data);
                buffer.data.clear();
            }
            writeFileLocked(); // 共享缓冲必须在这里落盘（threadBufferCapacity == 0 时它是唯一出口）
            buffer.fd = fd_.load(std::memory_order_relaxed);
            buffer.gen = gen_.load(std::memory_order_relaxed);
            buffer.counter = 0;
            rollPending_.store(false, std::memory_order_release);
        }

        void appendFilePerThread(std::string_view msg)
        {
            if (msg.empty())
                return;
            const Config &cfg = refConfig();
            const size_t tlsCapacity = cfg.threadBufferCapacity;
            TlsMultiple &buffer = tlsMultiple();
            if (tlsCapacity == 0)
            {
                writeMultipleFile(buffer, msg); // 无缓冲：直接写本线程的文件
                return;
            }

            buffer.data.append(msg.data(), msg.size());
            if (buffer.data.size() >= tlsCapacity)
            {
                flushMultiple(buffer);
                buffer.counter = 0;
            }
            else if ((++buffer.counter & 0xFFu) == 0)
            {
                const std::chrono::milliseconds interval(cfg.intervalFlushFile_ms);
                if (std::chrono::steady_clock::now() - buffer.lastFlush > interval)
                {
                    flushMultiple(buffer);
                    buffer.counter = 0;
                }
            }
        }

        void flushFilePerThread()
        {
            TlsMultiple &buffer = tlsMultiple();
            writeMultipleFile(buffer, buffer.data); // 数据为空时也会顺带处理 basename 切换
            buffer.data.clear();
            buffer.counter = 0;
        }

        // 回调绑定：函数指针 + ctx。thunk 无状态，所以 ctx 为空，实例在首次真正输出时
        // 才构造（保持惰性：配置回调不会提前打开日志文件）。返回值可直接交给
        // Logger，无需任何分配。
        inline static void fileOutputThunk(void * /*ctx*/, std::string_view msg)
        {
            getInstance().appendFile(msg);
        }
        inline static void fileFlushThunk(void * /*ctx*/) { getInstance().flushFile(); }

        inline static OutputFunction outputFunction_file() noexcept
        {
            return OutputFunction{&fileOutputThunk, nullptr};
        }
        inline static FlushFunction flushFunction_file() noexcept
        {
            return FlushFunction{&fileFlushThunk, nullptr};
        }

        // 每线程独立文件布局的绑定。契约与 outputFunction_file 完全相同，
        // 调用方可以随时在两者之间切换（切换时旧的 thread_local 缓冲会在退出/下次写入时排空）。
        inline static void perThreadOutputThunk(void * /*ctx*/, std::string_view msg)
        {
            getInstance().appendFilePerThread(msg);
        }
        inline static void perThreadFlushThunk(void * /*ctx*/)
        {
            getInstance().flushFilePerThread();
        }

        inline static OutputFunction outputFunction_filePerThread() noexcept
        {
            return OutputFunction{&perThreadOutputThunk, nullptr};
        }
        inline static FlushFunction flushFunction_filePerThread() noexcept
        {
            return FlushFunction{&perThreadFlushThunk, nullptr};
        }
    };

} // namespace mylog
