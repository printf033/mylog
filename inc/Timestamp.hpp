#include <ctime>
#include <string>
#include <cstdint>

#ifndef TIMESTAMP_HPP
#define TIMESTAMP_HPP

namespace mylog
{
    class Timestamp
    {
        std::int64_t microsec_;

    public:
        Timestamp();
        Timestamp(const std::int64_t t);
        ~Timestamp();
        operator std::int64_t() const;
        void swap(Timestamp &other);
        std::string toFormattedString(bool is_show_microsec = true) const;
        std::string toFormattedFile() const;
        bool valid() const;
        const std::int64_t getMicrosec() const;
        const std::int64_t getSecond() const;
        const std::int64_t getDay() const;
        void now();

    public:
        static Timestamp _Now();
        static Timestamp _Invalid();
    };

    inline std::int64_t differMicrosec(const Timestamp &a, const Timestamp &b)
    {
        return a.getMicrosec() - b.getMicrosec();
    }
    inline std::int64_t differSecond(const Timestamp &a, const Timestamp &b)
    {
        return a.getSecond() - b.getSecond();
    }
}
#endif