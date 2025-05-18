#include "fileManager.hpp"
#include <assert.h>
#include <error.h>
#include <string.h>
#include <unistd.h>

namespace mylog
{
    // 文件缓冲区容量（128kB）
    const int MAX_FILEBUFF_WRITE_TIMES = 131072;
    // 回滚文件时间间隔（s）
    const std::int64_t ROLL_FILE_INTERVAL = 3;

    FileManager::FileManager(const std::string &basename)
        : cur_write_times(0),
          last_roll_time_(Timestamp()),
          now_(Timestamp()),
          basename_(basename),
          pFileAppender_(nullptr)
    {
        rollFile();
    }
    FileManager::~FileManager()
    {
        delete pFileAppender_;
        pFileAppender_ = nullptr;
    }
    void FileManager::rollFile()
    {
        std::string filename(basename_);
        filename += now_.toUnformattedString();
        char buf[128] = {};
        filename += (0 == gethostname(buf, 128) ? std::string(buf) : std::string("unknownhost"));
        filename += std::to_string(getpid());
        filename += ".log";
        last_roll_time_ = now_;
        delete pFileAppender_;
        pFileAppender_ = new FileAppender(filename);
    }
    void FileManager::appendToBufferAutoRoll(const std::string &msg)
    {
        auto day_begin_time = now_.getDay();
        now_.now();
        if (now_.getDay() > day_begin_time ||                                      // 跨天回滚
            ++cur_write_times > MAX_FILEBUFF_WRITE_TIMES ||                        // 超过最大写入次数回滚
            now_.getSecond() - last_roll_time_.getSecond() > ROLL_FILE_INTERVAL || // 超过最大时间间隔回滚
            pFileAppender_->getContentSize() + msg.size() >= FILEBUFF_CAPA)        // 超过文件缓冲区大小回滚
        {
            flushBuffer();
            rollFile();
            cur_write_times = 0;
        }
        pFileAppender_->appendToBuffer(msg);
    }
    void FileManager::flushBuffer()
    {
        assert(pFileAppender_ != nullptr);
        pFileAppender_->flushBuffer();
    }
    void FileManager::appendToBufferAutoRoll_r(const std::string &msg)
    {
        std::lock_guard<std::mutex> locker(mtx_);
        appendToBufferAutoRoll(msg);
    }
    void FileManager::flushBuffer_r()
    {
        std::lock_guard<std::mutex> locker(mtx_);
        flushBuffer();
    }
    FileManager &FileManager::getInstance()
    {
        static FileManager instance("default");
        return instance;
    }
    void FileManager::setBasename(const std::string &basename)
    {
        getInstance().basename_ = basename;
    }
    void FileManager::outputFunc_file(const std::string &msg)
    {
        getInstance().appendToBufferAutoRoll(msg);
    }
    void FileManager::flushFunc_file()
    {
        getInstance().flushBuffer();
    }
    void FileManager::outputFunc_file_r(const std::string &msg)
    {
        getInstance().appendToBufferAutoRoll_r(msg);
    }
    void FileManager::flushFunc_file_r()
    {
        getInstance().flushBuffer_r();
    }
} // namespace mylog
