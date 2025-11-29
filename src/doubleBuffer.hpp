#ifndef DOUBLEBUFFER_HPP
#define DOUBLEBUFFER_HPP

#include "fileManager.hpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

class DoubleBuffer
{
    static const int PRE_BUFFER_CAPA = 131072;
    static inline const std::chrono::seconds SWAP_BUFFER_INTERVAL_s{3};

    std::string inputBuffer_;
    std::string outputBuffer_;
    std::mutex mtx_;
    std::condition_variable cvInputBufferFull_;
    std::condition_variable cvInputBufferEmpty_;
    std::vector<std::jthread> workThreadVec_;
    std::stop_source stopSource_;

private:
    DoubleBuffer()
    {
        inputBuffer_.reserve(PRE_BUFFER_CAPA);
        outputBuffer_.reserve(PRE_BUFFER_CAPA);
        workThreadVec_.emplace_back(
            std::jthread([this](std::stop_token st)
                         {
                             while (!st.stop_requested())
                             {
                                 {
                                     std::unique_lock<std::mutex> locker(mtx_);
                                     while (inputBuffer_.empty())
                                         cvInputBufferEmpty_.wait_for(locker, SWAP_BUFFER_INTERVAL_s);
                                     std::swap(inputBuffer_, outputBuffer_);
                                 }
                                 cvInputBufferFull_.notify_all();
                                 FileManager::getInstance().appendBuffer(outputBuffer_);
                                 FileManager::getInstance().flushBuffer();
                                 outputBuffer_.clear();
                             }
                             std::lock_guard<std::mutex> locker(mtx_);
                             if (!inputBuffer_.empty())
                                 FileManager::getInstance().appendBuffer(inputBuffer_); //
                         },
                         stopSource_.get_token()));
    }
    ~DoubleBuffer()
    {
        cvInputBufferFull_.notify_all();
        stopSource_.request_stop();
        cvInputBufferEmpty_.notify_all();
        std::lock_guard<std::mutex> locker(mtx_);
        if (!inputBuffer_.empty())
        {
            FileManager::getInstance().appendBuffer(inputBuffer_);
            FileManager::getInstance().flushBuffer();
        }
    }
    DoubleBuffer(const DoubleBuffer &) = delete;
    DoubleBuffer &operator=(const DoubleBuffer &) = delete;
    DoubleBuffer(DoubleBuffer &&) = delete;
    DoubleBuffer &operator=(DoubleBuffer &&) = delete;
    // n the number append
    // -1 buffer overflow (wait instead)
    // -2 stop requested
    int appendPreBuffer_r(const std::string &msg)
    {
        std::unique_lock<std::mutex> locker(mtx_);
        while (inputBuffer_.size() + msg.size() >= PRE_BUFFER_CAPA)
            cvInputBufferFull_.wait(locker);
        if (stopSource_.stop_requested())
            return -2;
        inputBuffer_.append(msg);
        cvInputBufferEmpty_.notify_all();
        return (int)msg.size();
    }
    // 0 success (notify worker to flush)
    int flushPreBuffer_r()
    {
        std::lock_guard<std::mutex> locker(mtx_);
        cvInputBufferEmpty_.notify_all();
        return 0;
    }
    static DoubleBuffer &getInstance()
    {
        static DoubleBuffer instance;
        return instance;
    }

public:
    static void outputFunction_2buffer_file_r(const std::string &msg) { getInstance().appendPreBuffer_r(msg); }
    static void flushFunction_2buffer_file_r() { getInstance().flushPreBuffer_r(); }
};

#endif