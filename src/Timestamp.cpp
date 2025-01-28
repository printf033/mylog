#include "Timestamp.hpp"
#include <sys/time.h>

namespace mylog
{
    // 缓冲区大小（单位：Byte）
    const int TIMESTAMP_BUFF_SIZE = 64;
    // 时间戳转换
    const std::int64_t MICROSEC_PER_SEC = 1000000;
    const std::int64_t MICROSEC_PER_DAY = 86400000000;

    Timestamp::Timestamp() : microsec_(0) {}
    Timestamp::Timestamp(const std::int64_t t) : microsec_(t) {}
    Timestamp::~Timestamp() {}
    Timestamp::operator std::int64_t() const { return microsec_; }
    void Timestamp::swap(Timestamp &other) { std::swap(microsec_, other.microsec_); }
    std::string Timestamp::toFormattedString(bool is_show_microsec) const
    {
        char buf[TIMESTAMP_BUFF_SIZE] = {};
        std::int64_t sec = microsec_ / MICROSEC_PER_SEC;
        std::int64_t micsec = microsec_ % MICROSEC_PER_SEC;
        struct tm tm_struc;
        localtime_r(&sec, &tm_struc);
        int len = sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
                          tm_struc.tm_year + 1900,
                          tm_struc.tm_mon + 1,
                          tm_struc.tm_mday,
                          tm_struc.tm_hour,
                          tm_struc.tm_min,
                          tm_struc.tm_sec);
        if (is_show_microsec)
            sprintf(buf + len, ".%ldZ", micsec);
        return std::string(buf);
    }
    std::string Timestamp::toFormattedFile() const
    {
        char buf[TIMESTAMP_BUFF_SIZE] = {};
        std::int64_t sec = microsec_ / MICROSEC_PER_SEC;
        struct tm tm_struc;
        localtime_r(&sec, &tm_struc);
        int len = sprintf(buf, "%04d%02d%02d%02d%02d%02d",
                          tm_struc.tm_year + 1900,
                          tm_struc.tm_mon + 1,
                          tm_struc.tm_mday,
                          tm_struc.tm_hour,
                          tm_struc.tm_min,
                          tm_struc.tm_sec);
        return std::string(buf);
    }
    bool Timestamp::valid() const { return microsec_ > 0; }
    const std::int64_t Timestamp::getMicrosec() const { return microsec_; }
    const std::int64_t Timestamp::getSecond() const { return microsec_ / MICROSEC_PER_SEC; }
    const std::int64_t Timestamp::getDay() const { return microsec_ / MICROSEC_PER_DAY; }
    void Timestamp::now() { *this = Timestamp::_Now(); }

    Timestamp Timestamp::_Now()
    {
        struct timeval tv_struc;
        gettimeofday(&tv_struc, nullptr);
        return Timestamp(tv_struc.tv_sec * MICROSEC_PER_SEC + tv_struc.tv_usec);
    }
    Timestamp Timestamp::_Invalid() { return Timestamp(0); }
} // namespace mylog
