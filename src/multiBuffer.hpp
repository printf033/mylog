#pragma once

#include "fileManager.hpp"
#include "timestamp.hpp"
#include <vector>
#include <cstring>
#include <mutex>

class MultiBuffer
{
    OptionsMultiBuffer options_;
    std::vector<char> preBuffer_;
    std::mutex mtx_;
    Timestamp lastFlushTime_;
    size_t pos_ = 0;

private:
    MultiBuffer() noexcept
        : options_(Configer::getInstance().optMultiBuffer)
    {
        preBuffer_.reserve(options_.preBufferCapacity);
    }
    ~MultiBuffer()
    {
        mtx_.lock();
        bool isFlush = !preBuffer_.empty();
        mtx_.unlock();
        if (isFlush)
            flushBuffer_r();
    }
    MultiBuffer(const MultiBuffer &) = delete;
    MultiBuffer &operator=(const MultiBuffer &) = delete;
    MultiBuffer(MultiBuffer &&) noexcept = delete;
    MultiBuffer &operator=(MultiBuffer &&) noexcept = delete;

public:
    inline static MultiBuffer &getInstance()
    {
        static MultiBuffer instance;
        return instance;
    }
    void appendBuffer_r(std::string_view msg)
    {
        const char *cursor = msg.data();
        size_t rest = msg.size();
        thread_local std::vector<char> localBuffer(options_.preBufferCapacity);
        while (rest > 0)
        {
            std::unique_lock<std::mutex> locker(mtx_);
            size_t idle = preBuffer_.size() - pos_;
            size_t n = std::min(rest, idle);
            std::memcpy(preBuffer_.data() + pos_, cursor, n);
            pos_ += n;
            cursor += n;
            rest -= n;
            if (lastFlushTime_.getDiffSeconds() > options_.intervalFlushBuffer ||
                pos_ >= preBuffer_.size())
            {
                std::swap(preBuffer_, localBuffer);
                size_t len = pos_;
                pos_ = 0;
                lastFlushTime_.update();
                locker.unlock();
                FileManager::getInstance().appendFile_r(std::string_view(localBuffer.data(), len));
            }
        }
    }
    void flushBuffer_r()
    {
        thread_local std::vector<char> localBuffer(options_.preBufferCapacity);
        mtx_.lock();
        std::swap(preBuffer_, localBuffer);
        size_t len = pos_;
        pos_ = 0;
        lastFlushTime_.update();
        mtx_.unlock();
        FileManager::getInstance().appendFile_r(std::string_view(localBuffer.data(), len));
    }
    inline static void outputFunction_buf_file_r(std::string_view msg) { getInstance().appendBuffer_r(msg); }
    inline static void flushFunction_buf_file_r() { getInstance().flushBuffer_r(); }
};