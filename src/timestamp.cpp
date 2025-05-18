#include "timestamp.hpp"
#include <sys/time.h>

namespace mylog
{
    // 时间戳转换
    const std::int64_t MICROSEC_PER_SEC = 1000000;
    const std::int64_t MICROSEC_PER_DAY = 86400000000;

    Timestamp::Timestamp() { now(); }
    Timestamp::Timestamp(const timeval &timeval_struc)
    {
        timeval_struc_.tv_sec = timeval_struc.tv_sec;
        timeval_struc_.tv_usec = timeval_struc.tv_usec;
    }
    Timestamp::Timestamp(const Timestamp &other)
    {
        timeval_struc_.tv_sec = other.timeval_struc_.tv_sec;
        timeval_struc_.tv_usec = other.timeval_struc_.tv_usec;
    }
    Timestamp &Timestamp::operator=(const Timestamp &other)
    {
        if (this != &other)
            Timestamp(other).swap(*this);
        return *this;
    }
    Timestamp::Timestamp(Timestamp &&other)
    {
        timeval_struc_.tv_sec = other.timeval_struc_.tv_sec;
        timeval_struc_.tv_usec = other.timeval_struc_.tv_usec;
        other.timeval_struc_.tv_sec = 0;
        other.timeval_struc_.tv_usec = 0;
    }
    Timestamp &Timestamp::operator=(Timestamp &&other)
    {
        if (this != &other)
            Timestamp(std::move(other)).swap(*this);
        return *this;
    }
    std::string Timestamp::toFormattedString(bool is_show_microsec) const
    {
        char buf[32] = {};
        struct tm tm_struc;
        localtime_r(&timeval_struc_.tv_sec, &tm_struc);
        int len = sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d",
                          tm_struc.tm_year + 1900,
                          tm_struc.tm_mon + 1,
                          tm_struc.tm_mday,
                          tm_struc.tm_hour,
                          tm_struc.tm_min,
                          tm_struc.tm_sec);
        if (is_show_microsec)
            sprintf(buf + len, ".%06ld", timeval_struc_.tv_usec);
        return std::string(buf);
    }
    std::string Timestamp::toUnformattedString() const
    {
        char buf[16] = {};
        struct tm tm_struc;
        localtime_r(&timeval_struc_.tv_sec, &tm_struc);
        int len = sprintf(buf, "%04d%02d%02d%02d%02d%02d",
                          tm_struc.tm_year + 1900,
                          tm_struc.tm_mon + 1,
                          tm_struc.tm_mday,
                          tm_struc.tm_hour,
                          tm_struc.tm_min,
                          tm_struc.tm_sec);
        return std::string(buf);
    }
    const std::int64_t Timestamp::getMicrosec() const { return timeval_struc_.tv_sec * MICROSEC_PER_SEC + timeval_struc_.tv_usec; }
    const std::int64_t Timestamp::getSecond() const { return timeval_struc_.tv_sec; }
    const std::int64_t Timestamp::getDay() const { return timeval_struc_.tv_usec / MICROSEC_PER_DAY; }
    void Timestamp::now()
    {
        struct timeval timeval_struc;
        gettimeofday(&timeval_struc, nullptr);
        timeval_struc_.tv_sec = timeval_struc.tv_sec;
        timeval_struc_.tv_usec = timeval_struc.tv_usec;
    }

    void Timestamp::swap(Timestamp &other)
    {
        std::swap(timeval_struc_, other.timeval_struc_);
    }
} // namespace mylog
