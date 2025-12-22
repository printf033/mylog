#pragma once

#include <ctime>
#include <string>
#include <cstdint>
#include <sys/time.h>

class Timestamp
{
    struct timeval timeval_struc_;

public:
    void update()
    {
        struct timeval timeval_struc;
        gettimeofday(&timeval_struc, nullptr);
        timeval_struc_.tv_sec = timeval_struc.tv_sec;
        timeval_struc_.tv_usec = timeval_struc.tv_usec;
    }
    Timestamp() { update(); }
    ~Timestamp() = default;
    void swap(Timestamp &other) { std::swap(timeval_struc_, other.timeval_struc_); }
    Timestamp(const Timestamp &other)
    {
        timeval_struc_.tv_sec = other.timeval_struc_.tv_sec;
        timeval_struc_.tv_usec = other.timeval_struc_.tv_usec;
    }
    Timestamp &operator=(const Timestamp &other)
    {
        if (this != &other)
            Timestamp(other).swap(*this);
        return *this;
    }
    Timestamp(Timestamp &&other)
    {
        timeval_struc_.tv_sec = other.timeval_struc_.tv_sec;
        timeval_struc_.tv_usec = other.timeval_struc_.tv_usec;
        other.timeval_struc_.tv_sec = 0;
        other.timeval_struc_.tv_usec = 0;
    }
    Timestamp &operator=(Timestamp &&other)
    {
        if (this != &other)
            Timestamp(std::move(other)).swap(*this);
        return *this;
    }
    std::string toFormattedString(bool is_show_microsec = true) const
    {
        char buf[72] = {};
        struct tm tm_struc;
        localtime_r(&timeval_struc_.tv_sec, &tm_struc);
        int len = sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
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
    std::string toUnformattedString() const
    {
        char buf[67] = {};
        struct tm tm_struc;
        localtime_r(&timeval_struc_.tv_sec, &tm_struc);
        sprintf(buf, "%04d%02d%02d%02d%02d%02d",
                tm_struc.tm_year + 1900,
                tm_struc.tm_mon + 1,
                tm_struc.tm_mday,
                tm_struc.tm_hour,
                tm_struc.tm_min,
                tm_struc.tm_sec);
        return std::string(buf);
    }
    std::int64_t getSecond() const { return timeval_struc_.tv_sec; }
    std::int64_t getDay() const { return timeval_struc_.tv_usec / 86400000000L; }
};