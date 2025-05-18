#include "fileAppender.hpp"
#include "timestamp.hpp"
#include <string>
#include <mutex>

#ifndef FILEMANAGER_HPP
#define FILEMANAGER_HPP

namespace mylog
{
    class FileManager
    {
        int cur_write_times;
        Timestamp last_roll_time_;
        Timestamp now_;
        std::string basename_;
        FileAppender* pFileAppender_;
        std::mutex mtx_;

    private:
        FileManager(const std::string &basename);
        ~FileManager();
        FileManager(const FileManager &) = delete;
        FileManager &operator=(const FileManager &) = delete;
        FileManager(FileManager &&) = delete;
        FileManager &operator=(FileManager &&) = delete;
        void rollFile();

    public:
        // 客户端线程调用
        static FileManager &getInstance();
        void appendToBufferAutoRoll(const std::string &msg);
        void flushBuffer();
        void appendToBufferAutoRoll_r(const std::string &msg);
        void flushBuffer_r();
        // Logger接口设置函数
        static void setBasename(const std::string &basename);
        static void outputFunc_file(const std::string &msg);
        static void flushFunc_file();
        static void outputFunc_file_r(const std::string &msg);
        static void flushFunc_file_r();
    };
}

#endif