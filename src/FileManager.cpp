#include "FileManager.hpp"
#include <assert.h>
#include <error.h>
#include <string.h>
#include <unistd.h>

namespace mylog
{
    // 主机名缓冲区
    const int HOSTNAME_BUFF_SIZE = 64;
    // 时间戳转换
    const time_t SEC_PER_DAY = 86400;
    // 文件缓冲区容量（128kB）
    const int MAX_FILEBUFF_WRITE_TIMES = 131072;
    // 刷新文件缓冲区时间间隔
    const time_t FLUSH_FILEBUFF_INTERVAL = 3;

    FileManager::FileManager(const std::string &basename)
        : cur_write_times(0),
          day_begin_time_(0),
          last_flush_time_(0),
          last_roll_time_(0),
          basename_(basename),
          puTimestamp_(new Timestamp()),
          pumutex_(new std::mutex()),
          puFileAppender_(nullptr) {}
    FileManager::~FileManager() {}
    std::string FileManager::getLogFilename()
    {
        std::string filename(basename_);
        puTimestamp_->now();
        filename += puTimestamp_->toFormattedFile();
        char buf[HOSTNAME_BUFF_SIZE] = {};
        filename += (0 == gethostname(buf, HOSTNAME_BUFF_SIZE) ? std::string(buf) : std::string("unknownhost"));
        filename += std::to_string(getpid());
        filename += ".log";
        return filename;
    }
    void FileManager::rollFile()
    {
        flushBuffer();
        std::string filename = getLogFilename();
        last_roll_time_ = puTimestamp_->getSecond();
        puFileAppender_.reset(new FileAppender(filename));
    }
    void FileManager::appendToBufferAutoRoll(const std::string &msg)
    {
        time_t now = time(nullptr);
        time_t day_begin_time = now / SEC_PER_DAY * SEC_PER_DAY; // 这天开始时的秒数
        if (day_begin_time != day_begin_time_)
        {
            rollFile();
            day_begin_time_ = day_begin_time;
            cur_write_times = 0;
        }
        else if (++cur_write_times > MAX_FILEBUFF_WRITE_TIMES)
        {
            rollFile();
            cur_write_times = 0;
        }
        else if (puFileAppender_->getContentSize() + msg.size() >= FILEBUFF_CAPA)
        {
            rollFile();
        }
        else if (now - last_flush_time_ > FLUSH_FILEBUFF_INTERVAL)
        {
            flushBuffer();
        }
        puFileAppender_->appendToBuffer(msg);
    }
    void FileManager::flushBuffer()
    {
        if (puFileAppender_)
        {
            puFileAppender_->flushBuffer();
            last_flush_time_ = puTimestamp_->getSecond();
        }
    }
    void FileManager::appendToBufferAutoRoll_r(const std::string &msg)
    {
        std::unique_lock<std::mutex> locker(*pumutex_);
        appendToBufferAutoRoll(msg);
    }
    void FileManager::flushBuffer_r()
    {
        std::unique_lock<std::mutex> locker(*pumutex_);
        flushBuffer();
    }
    FileManager &FileManager::_getInstance()
    {
        static FileManager instance("default");
        return instance;
    }
    void FileManager::_setBasename(const std::string &basename)
    {
        _getInstance().basename_ = basename;
    }
    void FileManager::_outputFunc_file(const std::string &msg)
    {
        _getInstance().appendToBufferAutoRoll(msg);
    }
    void FileManager::_flushFunc_file()
    {
        _getInstance().flushBuffer();
    }
    void FileManager::_outputFunc_file_r(const std::string &msg)
    {
        _getInstance().appendToBufferAutoRoll_r(msg);
    }
    void FileManager::_flushFunc_file_r()
    {
        _getInstance().flushBuffer_r();
    }
} // namespace mylog
