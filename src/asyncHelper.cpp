#include "asyncHelper.hpp"
#include <assert.h>
#include <error.h>
#include <string.h>
#include <unistd.h>
#include <iostream>

namespace mylog
{
    // 异步预缓冲区容量（128kB）
    const int ASYNCBUFF_CAPA = 131072;
    // 交换异步预缓冲区时间间隔
    const std::chrono::seconds SWAP_ASYNCBUFF_INTERVAL(3);

    AsyncHelper::AsyncHelper()
        : is_running_(true),
          puInputBuffer_(new std::string),
          puOutputBuffer_(new std::string)
    {
        puInputBuffer_->reserve(ASYNCBUFF_CAPA);
        puOutputBuffer_->reserve(ASYNCBUFF_CAPA);
        puthread_.reset(new std::thread(&AsyncHelper::workThreadFunc, this));
    }
    AsyncHelper &AsyncHelper::getInstance()
    {
        static AsyncHelper instance;
        return instance;
    }
    void AsyncHelper::workThreadFunc()
    {
        while (is_running_)
        {
            {
                std::unique_lock<std::mutex> locker(mtx_);
                while (is_running_ && puInputBuffer_->empty())
                    cv_InputBuffer_empty_.wait_for(locker, SWAP_ASYNCBUFF_INTERVAL);
                std::swap(*puInputBuffer_, *puOutputBuffer_);
            }
            cv_InputBuffer_full_.notify_all();
            // 此处使用非_r函数，必须确保没有其他线程使用FileManager单例！
            FileManager::getInstance().appendToBufferAutoRoll(*puOutputBuffer_);
            puOutputBuffer_->clear();
        }
    }
    void AsyncHelper::appendToPreBuffer_r(const std::string &msg)
    {
        std::unique_lock<std::mutex> locker(mtx_);
        while (is_running_ && puInputBuffer_->size() + msg.size() >= ASYNCBUFF_CAPA)
            cv_InputBuffer_full_.wait(locker);
        if (!is_running_)
            return;
        puInputBuffer_->append(msg);
        cv_InputBuffer_empty_.notify_all();
    }
    void AsyncHelper::flushPreBuffer_r()
    {
        std::lock_guard<std::mutex> locker(mtx_);
        // 此处使用非_r函数，必须确保没有其他线程使用FileManager单例！
        FileManager::getInstance().flushBuffer();
    }
    AsyncHelper::~AsyncHelper()
    {
        {
            std::lock_guard<std::mutex> locker(mtx_);
            is_running_ = false;
        }
        cv_InputBuffer_full_.notify_all();
        cv_InputBuffer_empty_.notify_all();
        if (puthread_->joinable())
            puthread_->join();
    }

    AsyncHelper_stdout::AsyncHelper_stdout()
        : is_running_(true),
          puInputBuffer_(new std::string),
          puOutputBuffer_(new std::string)
    {
        puInputBuffer_->reserve(ASYNCBUFF_CAPA);
        puOutputBuffer_->reserve(ASYNCBUFF_CAPA);
        puthread_.reset(new std::thread(&AsyncHelper_stdout::workThreadFunc, this));
    }
    AsyncHelper_stdout &AsyncHelper_stdout::getInstance()
    {
        static AsyncHelper_stdout instance;
        return instance;
    }
    void AsyncHelper_stdout::workThreadFunc()
    {
        while (is_running_)
        {
            {
                std::unique_lock<std::mutex> locker(mtx_);
                while (is_running_ && puInputBuffer_->empty())
                    cv_InputBuffer_empty_.wait_for(locker, SWAP_ASYNCBUFF_INTERVAL);
                std::swap(*puInputBuffer_, *puOutputBuffer_);
            }
            cv_InputBuffer_full_.notify_all();
            printf("%s", puOutputBuffer_->c_str());
            puOutputBuffer_->clear();
        }
    }
    void AsyncHelper_stdout::appendToPreBuffer_r(const std::string &msg)
    {
        std::unique_lock<std::mutex> locker(mtx_);
        while (is_running_ && puInputBuffer_->size() + msg.size() >= ASYNCBUFF_CAPA)
            cv_InputBuffer_full_.wait(locker);
        if (!is_running_)
            return;
        puInputBuffer_->append(msg);
        cv_InputBuffer_empty_.notify_all();
    }
    void AsyncHelper_stdout::flushPreBuffer_r()
    {
        std::lock_guard<std::mutex> locker(mtx_);
        if (is_running_)
            fflush(stdout);
    }
    AsyncHelper_stdout::~AsyncHelper_stdout()
    {
        {
            std::lock_guard<std::mutex> locker(mtx_);
            is_running_ = false;
        }
        cv_InputBuffer_full_.notify_all();
        cv_InputBuffer_empty_.notify_all();
        if (puthread_->joinable())
            puthread_->join();
    }

    void AsyncHelper::outputFunc_async_file(const std::string &msg)
    {
        getInstance().appendToPreBuffer_r(msg);
    }
    void AsyncHelper::flushFunc_async_file()
    {
        getInstance().flushPreBuffer_r();
    }
    void AsyncHelper_stdout::outputFunc_async_stdout(const std::string &msg)
    {
        getInstance().appendToPreBuffer_r(msg);
    }
    void AsyncHelper_stdout::flushFunc_async_stdout()
    {
        getInstance().flushPreBuffer_r();
    }
} // namespace mylog
