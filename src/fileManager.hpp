#pragma once

#include "fileAppender.hpp"
#include "timestamp.hpp"
#include <string>
#include <mutex>
#include <unistd.h>

class FileManager
{
    static inline std::string basename_global = "log_";
    static const int MAX_WRITE_TIMES = 1000;
    static const std::int64_t ROLL_FILE_INTERVAL_s = 3;

    int curWriteTimes_ = 0;
    Timestamp lastRollTime_{};
    Timestamp time_{};
    FileAppender *pFileAppender_ = nullptr;
    std::mutex mtx_;

private:
    void rollFile()
    {
        std::string filename(basename_global);
        filename += time_.toUnformattedString();
        char buf[128] = {};
        filename += (0 == gethostname(buf, 128) ? std::string(buf) : std::string("unknownhost"));
        filename += std::to_string(getpid());

        lastRollTime_ = time_;
        delete pFileAppender_;
        pFileAppender_ = new FileAppender(filename);
    }
    FileManager()
    {
        std::lock_guard<std::mutex> locker(mtx_);
        rollFile();
    }
    ~FileManager()
    {
        std::lock_guard<std::mutex> locker(mtx_);
        delete pFileAppender_;
        pFileAppender_ = nullptr;
    }
    FileManager(const FileManager &) = delete;
    FileManager &operator=(const FileManager &) = delete;
    FileManager(FileManager &&) = delete;
    FileManager &operator=(FileManager &&) = delete;

public:
    static FileManager &getInstance()
    {
        static FileManager instance;
        return instance;
    }
    // n the number append
    // -1 buffer overflow
    int appendBuffer(const std::string &msg)
    {
        time_.update();
        if (time_.getDay() > lastRollTime_.getDay() ||
            ++curWriteTimes_ > MAX_WRITE_TIMES ||
            time_.getSecond() - lastRollTime_.getSecond() > ROLL_FILE_INTERVAL_s)
        {
            rollFile();
            curWriteTimes_ = 0;
        }
        assert(pFileAppender_ != nullptr);
        return pFileAppender_->appendBuffer(msg);
    }
    // 0 success
    int flushBuffer()
    {
        assert(pFileAppender_ != nullptr);
        return pFileAppender_->flushBuffer();
    }
    // n the number append
    // -1 buffer overflow
    int appendBuffer_r(const std::string &msg)
    {
        std::lock_guard<std::mutex> locker(mtx_);
        return appendBuffer(msg);
    }
    // 0 success
    int flushBuffer_r()
    {
        std::lock_guard<std::mutex> locker(mtx_);
        return flushBuffer();
    }

    static void setBasename(const std::string &basename) { basename_global = basename; }
    static void outputFunction_file(const std::string &msg) { getInstance().appendBuffer(msg); }
    static void flushFunction_file() { getInstance().flushBuffer(); }
    static void outputFunction_file_r(const std::string &msg) { getInstance().appendBuffer_r(msg); }
    static void flushFunction_file_r() { getInstance().flushBuffer_r(); }
};