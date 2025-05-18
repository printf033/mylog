#include <ctime>
#include <string>
#include <cstdint>

#ifndef TIMESTAMP_HPP
#define TIMESTAMP_HPP

namespace mylog
{
    class Timestamp
    {
        struct timeval timeval_struc_;

    public:
        Timestamp();
        Timestamp(const timeval &timeval_struc);
        ~Timestamp() = default;
        Timestamp(const Timestamp &other);
        Timestamp &operator=(const Timestamp &other);
        Timestamp(Timestamp &&other);
        Timestamp &operator=(Timestamp &&other);
        std::string toFormattedString(bool is_show_microsec = true) const;
        std::string toUnformattedString() const;
        const std::int64_t getMicrosec() const;
        const std::int64_t getSecond() const;
        const std::int64_t getDay() const;
        void now();

    private:
        void swap(Timestamp &other);
    };
}
#endif