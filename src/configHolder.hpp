#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

namespace mylog
{

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

        void publish(T value)
        {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                value_ = std::move(value);
            }
            generation_.fetch_add(1, std::memory_order_release);
        }

        T copy() const
        {
            std::lock_guard<std::mutex> lock(mtx_);
            return value_;
        }

        uint32_t getGeneration() const noexcept
        {
            return generation_.load(std::memory_order_acquire);
        }
    };

    template <typename T>
    class ConfigSnapshot
    {
        const ConfigHolder<T> &holder_;
        T local_{};
        uint32_t generation_ = 0;

    public:
        explicit ConfigSnapshot(const ConfigHolder<T> &holder) : holder_(holder) {}
        ConfigSnapshot(const ConfigSnapshot &) = delete;
        ConfigSnapshot &operator=(const ConfigSnapshot &) = delete;

        inline const T &refConfig() noexcept
        {
            const uint32_t gen = holder_.getGeneration();
            if (gen != generation_)
            {
                local_ = holder_.copy();
                generation_ = gen;
            }
            return local_;
        }
    };
}
