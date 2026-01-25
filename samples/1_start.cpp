#include "logger.hpp"

int main()
{
    LOG_TRACE("mylog {}", 1);
    LOG_DEBUG("mylog {}", 2);
    LOG_INFO("mylog {}", 3);
    LOG_WARN("mylog {}", 4);
    LOG_ERROR("mylog {}", 5);
    LOG_FATAL("mylog {}", 6);
}