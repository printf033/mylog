#include "fileManager.hpp"
#include <thread>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>

#ifndef FILEASYNCHELPER_HPP
#define FILEASYNCHELPER_HPP

namespace mylog
{
    class AsyncHelper // 多生产者（客户线程） 单消费者（工作线程） 同步队列（双buffer）
    {
        // 当前同步缓冲区
        std::unique_ptr<std::string> puInputBuffer_;
        std::unique_ptr<std::string> puOutputBuffer_;

        // 用于同步（buffer满了就交换buffer）
        std::mutex mtx_;
        std::condition_variable cv_InputBuffer_full_;  // 叫工作线程起床交换buffer
        std::condition_variable cv_InputBuffer_empty_; // 定时交换buffer
        bool is_running_;

        // 工作线程
        std::unique_ptr<std::thread> puthread_;

    private:
        AsyncHelper();
        ~AsyncHelper();
        AsyncHelper(const AsyncHelper &) = delete;
        AsyncHelper &operator=(const AsyncHelper &) = delete;
        AsyncHelper(AsyncHelper &&) = delete;
        AsyncHelper &operator=(AsyncHelper &&) = delete;
        // 工作线程调用
        void workThreadFunc();
        // 客户端线程调用
        void appendToPreBuffer_r(const std::string &msg);
        void flushPreBuffer_r();
        static AsyncHelper &getInstance();

    public:
        // Logger接口设置函数
        static void outputFunc_async_file(const std::string &msg);
        static void flushFunc_async_file();
    };

    class AsyncHelper_stdout // 给stdout用（如果操作系统保证printf线程安全的话没必要使用）
    {
        std::unique_ptr<std::string> puInputBuffer_;
        std::unique_ptr<std::string> puOutputBuffer_;
        std::mutex mtx_;
        std::condition_variable cv_InputBuffer_full_;
        std::condition_variable cv_InputBuffer_empty_;
        bool is_running_;
        std::unique_ptr<std::thread> puthread_;

    private:
        AsyncHelper_stdout();
        ~AsyncHelper_stdout();
        AsyncHelper_stdout(const AsyncHelper_stdout &) = delete;
        AsyncHelper_stdout &operator=(const AsyncHelper_stdout &) = delete;
        AsyncHelper_stdout(AsyncHelper_stdout &&) = delete;
        AsyncHelper_stdout &operator=(AsyncHelper_stdout &&) = delete;
        void workThreadFunc();
        void appendToPreBuffer_r(const std::string &msg);
        void flushPreBuffer_r();
        static AsyncHelper_stdout &getInstance();

    public:
        // Logger接口设置函数
        static void outputFunc_async_stdout(const std::string &msg);
        static void flushFunc_async_stdout();
    };
}

#endif