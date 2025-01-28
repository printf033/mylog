#include "FileAppender.hpp"
#include "Timestamp.hpp"
#include <string>
#include <memory>
#include <mutex>

#ifndef FILEMANAGER_HPP
#define FILEMANAGER_HPP

namespace mylog
{
    class FileManager
    {
        int cur_write_times;
        time_t day_begin_time_;
        time_t last_flush_time_;
        time_t last_roll_time_;
        std::string basename_;
        std::unique_ptr<Timestamp> puTimestamp_;
        std::unique_ptr<FileAppender> puFileAppender_;
        std::unique_ptr<std::mutex> pumutex_;

    private:
        FileManager(const std::string &basename);
        ~FileManager();
        FileManager(const FileManager &) = delete;
        FileManager &operator=(const FileManager &) = delete;
        std::string getLogFilename();
        void rollFile();

    public:
        // 客户端线程调用
        static FileManager &_getInstance();
        void appendToBufferAutoRoll(const std::string &msg);
        void flushBuffer();
        void appendToBufferAutoRoll_r(const std::string &msg);
        void flushBuffer_r();
        // Logger接口设置函数
        static void _setBasename(const std::string &basename);
        static void _outputFunc_file(const std::string &msg);
        static void _flushFunc_file();
        static void _outputFunc_file_r(const std::string &msg);
        static void _flushFunc_file_r();
    };
}

#endif