#include <string>
#include <memory>
#include <mutex>

#ifndef FILEAPPENDER_HPP
#define FILEAPPENDER_HPP

namespace mylog
{
    // 文件缓冲区容量（128kB）
    extern const int FILEBUFF_CAPA;

    class FileAppender // 非线程安全
    {
        FILE *pFILE_;
        size_t content_size_;
        std::unique_ptr<char[]> puBuffer_;

    public:
        FileAppender(const std::string &filename);
        ~FileAppender();
        void appendToBuffer(const std::string &msg);
        void flushBuffer();
        size_t getContentSize() const;
    };
}

#endif