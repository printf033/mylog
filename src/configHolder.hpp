#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

namespace mylog
{

    /**
     * @brief 线程安全配置容器（发布者）
     *
     * 采用版本代次（Generation Counter）机制实现发布-订阅模式。
     * 配置写入（发布）由互斥锁保护，并通过原子代次递增对外发布版本更新；
     * 配合 ConfigSnapshot 可实现读路径零锁、零竞争的高性能无锁快照访问。
     *
     * @tparam T 配置对象类型，要求支持移动构造与拷贝构造
     */
    template <typename T>
    class ConfigHolder
    {
        mutable std::mutex mtx_;
        T value_{};
        std::atomic<uint32_t> generation_{1};

    public:
        ConfigHolder() = default;
        ConfigHolder(const ConfigHolder &) = delete;
        ConfigHolder &operator=(const ConfigHolder &) = delete;

        /**
         * @brief 发布新配置并递增版本代次
         *
         * 在互斥锁保护下更新内部配置对象，并通过 release 内存序递增代次，
         * 通知所有持有 ConfigSnapshot 的读取线程在下次访问时同步最新配置。
         *
         * @param value 新的配置实例（通过移动语义传入以避免冗余拷贝）
         */
        void publish(T value)
        {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                value_ = std::move(value);
            }
            generation_.fetch_add(1, std::memory_order_release);
        }

        /**
         * @brief 复制当前配置实例
         *
         * 在互斥锁保护下深拷贝当前保存的配置对象，供快照同步或外部查询使用。
         *
         * @return T 当前配置对象的完整副本
         */
        T duplicate() const
        {
            std::lock_guard<std::mutex> lock(mtx_);
            return value_;
        }

        /**
         * @brief 获取当前配置的版本代次
         *
         * 使用 acquire 内存序读取原子代次计数器，用于无锁快速判断配置是否已被修改。
         *
         * @return uint32_t 当前配置版本代次号
         */
        uint32_t getGeneration() const noexcept { return generation_.load(std::memory_order_acquire); }
    };

    /**
     * @brief 线程局部配置快照（订阅者）
     *
     * 绑定至指定的 ConfigHolder，通常作为 thread_local 变量使用。
     * 每次访问时仅需一次原子 acquire 读校验代次；若代次未变则直接返回线程私有引用的只读配置，
     * 做到热路径完全零锁、零内存拷贝与零缓存行争用。
     *
     * @tparam T 配置对象类型
     */
    template <typename T>
    class ConfigSnapshot
    {
        const ConfigHolder<T> &holder_;
        T local_{};
        uint32_t generation_ = 0;

    public:
        /**
         * @brief 构造配置快照并绑定目标容器
         *
         * @param holder 目标配置容器的常量引用，生命周期须覆盖此快照
         */
        explicit ConfigSnapshot(const ConfigHolder<T> &holder)
            : holder_(holder)
        {
        }
        ConfigSnapshot(const ConfigSnapshot &) = delete;
        ConfigSnapshot &operator=(const ConfigSnapshot &) = delete;

        /**
         * @brief 获取最新配置的常量引用
         *
         * 快速检查容器版本代次。若版本发生递增，则加锁拉取最新配置副本至本线程私有缓存，
         * 并同步代次号；若未更新则直接返回本线程缓存的配置引用。
         *
         * @return const T& 当前生效配置的常量引用
         */
        inline const T &refConfig() noexcept
        {
            const uint32_t gen = holder_.getGeneration();
            if (gen != generation_)
            {
                local_ = holder_.duplicate();
                generation_ = gen;
            }
            return local_;
        }
    };
} // namespace mylog
