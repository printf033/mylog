#pragma once

#include <chrono>
#include <string>
#include <format>

class Timestamp
{
    std::chrono::time_point<std::chrono::system_clock> lastTimepoint_;

public:
    Timestamp() noexcept { lastTimepoint_ = std::chrono::system_clock::now(); }
    ~Timestamp() noexcept = default;
    Timestamp(const Timestamp &) noexcept = default;
    Timestamp &operator=(const Timestamp &) noexcept = default;
    Timestamp(Timestamp &&) noexcept = default;
    Timestamp &operator=(Timestamp &&) noexcept = default;
    inline void swap(Timestamp &other) noexcept
    {
        std::swap(lastTimepoint_, other.lastTimepoint_);
    }
    inline std::chrono::time_point<std::chrono::system_clock> update() noexcept
    {
        return lastTimepoint_ = std::chrono::system_clock::now();
    }
    inline std::string toString() const
    {
        return std::format("{:%Y%m%d_%H%M%S}", std::chrono::floor<std::chrono::seconds>(lastTimepoint_));
    }
    inline std::string toFormattedString() const
    {
        return std::format("{:%F %T}", lastTimepoint_);
    }
    inline std::chrono::seconds getDiffSeconds() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - lastTimepoint_);
    }
    inline std::chrono::days getDiffDays() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::days>(std::chrono::system_clock::now() - lastTimepoint_);
    }
};