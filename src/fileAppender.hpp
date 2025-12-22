#pragma once

#include <string>
#include <cassert>

class FileAppender
{
    static const int FILE_BUFFER_CAPA = 131072;

    char *pBuffer_ = nullptr;
    FILE *pFILE_ = nullptr;
    size_t content_size_ = 0;

public:
    FileAppender(const std::string &filename)
    {
        pBuffer_ = new char[FILE_BUFFER_CAPA];
        pFILE_ = fopen(filename.c_str(), "a");
        assert(nullptr != pFILE_);
        setvbuf(pFILE_, pBuffer_, _IOFBF, FILE_BUFFER_CAPA);
    }
    ~FileAppender()
    {
        fflush(pFILE_);
        fclose(pFILE_);
        pFILE_ = nullptr;
        delete[] pBuffer_;
        pBuffer_ = nullptr;
    }
    FileAppender(const FileAppender &) = delete;
    FileAppender &operator=(const FileAppender &) = delete;
    FileAppender(FileAppender &&) = delete;
    FileAppender &operator=(FileAppender &&) = delete;
    // n the number append
    // -1 buffer overflow
    int appendBuffer(const std::string &msg)
    {
        const size_t total = msg.size();
        if (content_size_ + total >= FILE_BUFFER_CAPA)
            return -1;
        size_t sum = 0;
        const char *pmsg = msg.c_str();
        while (sum < total)
            sum += fwrite_unlocked(pmsg + sum, sizeof(char), total - sum, pFILE_);
        content_size_ += sum;
        return (int)sum;
    }
    // 0 success
    int flushBuffer() { return fflush(pFILE_); }
};