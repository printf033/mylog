#pragma once

#include "myconfiger.hpp"
#include "timestamp.hpp"
#include <string>
#include <mutex>
#include <unistd.h>

class FileManager
{
    OptionsFileManager options_;
    FILE *pFile_ = nullptr;
    size_t fileSize_ = 0;
    char *pBuffer_ = nullptr;
    Timestamp lastFlushTime_;
    std::mutex mtx_;

private:
    FileManager() noexcept
        : options_(Configer::getInstance().optFileManager)
    {
        pBuffer_ = new char[options_.fileBufferCapacity];
        lastFlushTime_.update();
        pFile_ = fopen(
            std::format("{}_{}_{}.log", lastFlushTime_.toString(), options_.basename, getpid()).c_str(),
            "a");
        if (pBuffer_ != nullptr && pFile_ != nullptr)
            setvbuf(pFile_, pBuffer_, _IOFBF, options_.fileBufferCapacity);
    }
    ~FileManager() noexcept
    {
        std::lock_guard<std::mutex> locker(mtx_);
        if (pFile_ != nullptr)
        {
            fflush(pFile_);
            fclose(pFile_);
            pFile_ = nullptr;
        }
        if (pBuffer_ != nullptr)
        {
            delete[] pBuffer_;
            pBuffer_ = nullptr;
        }
    }
    FileManager(const FileManager &) = delete;
    FileManager &operator=(const FileManager &) = delete;
    FileManager(FileManager &&) noexcept = delete;
    FileManager &operator=(FileManager &&) noexcept = delete;

public:
    inline static FileManager &getInstance()
    {
        static FileManager instance;
        return instance;
    }
    void appendFile(std::string_view msg)
    {
        if (pFile_ == nullptr)
            return;
        if (lastFlushTime_.getDiffDays() > std::chrono::days(0) ||
            fileSize_ >= options_.fileCapacity)
        {
            fflush(pFile_);
            fclose(pFile_);
            pFile_ = nullptr;
            lastFlushTime_.update();
            pFile_ = fopen(
                std::format("{}_{}_{}.log", lastFlushTime_.toString(), options_.basename, getpid()).c_str(),
                "a");
            fileSize_ = 0;
        }
        if (lastFlushTime_.getDiffSeconds() > options_.intervalFlushFile)
        {
            fflush(pFile_);
            lastFlushTime_.update();
        }
        fileSize_ += fwrite_unlocked(msg.data(), 1, msg.size(), pFile_);
    }
    void flushFile()
    {
        if (pFile_ == nullptr)
            return;
        if (fflush(pFile_) < 0)
            return;
    }
    void appendFile_r(std::string_view msg)
    {
        std::lock_guard<std::mutex> locker(mtx_);
        appendFile(msg);
    }
    void flushFile_r()
    {
        std::lock_guard<std::mutex> locker(mtx_);
        flushFile();
    }
    inline static void outputFunction_file(std::string_view msg)
    {
        getInstance().appendFile(msg);
    }
    inline static void flushFunction_file()
    {
        getInstance().flushFile();
    }
    inline static void outputFunction_file_r(std::string_view msg)
    {
        getInstance().appendFile_r(msg);
    }
    inline static void flushFunction_file_r()
    {
        getInstance().flushFile_r();
    }
};