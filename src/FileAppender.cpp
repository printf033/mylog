#include "FileAppender.hpp"
#include <assert.h>
#include <error.h>
#include <string.h>

namespace mylog
{
    // 文件缓冲区容量（128kB）
    const int FILEBUFF_CAPA = 131072;

    FileAppender::FileAppender(const std::string &filename)
        : pFILE_(nullptr),
          content_size_(0),
          puBuffer_(new char[FILEBUFF_CAPA])
    {
        pFILE_ = fopen(filename.c_str(), "a");
        assert(pFILE_ != nullptr);
        setvbuf(pFILE_, puBuffer_.get(), _IOFBF, FILEBUFF_CAPA);
    }
    FileAppender::~FileAppender() { fclose(pFILE_); }
    void FileAppender::appendToBuffer(const std::string &msg)
    {
        const char *info = msg.c_str();
        const size_t len = msg.size();
        size_t sum = 0;
        while (sum < len)
        {
            size_t n = fwrite_unlocked(info + sum, sizeof(char), len - sum, pFILE_);
            if (0 == n)
            {
                fprintf(stderr, "****** FileAppender append Failed: %s ******\n", strerror(ferror(pFILE_)));
                return;
            }
            sum += n;
        }
        content_size_ += sum;
    }
    void FileAppender::flushBuffer()
    {
        if (nullptr != pFILE_)
            fflush(pFILE_);
    }
    size_t FileAppender::getContentSize() const { return content_size_; }
} // namespace mylog
