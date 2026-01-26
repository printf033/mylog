#include "logger.hpp"

int main(int argc, char *argv[])
{
    std::filesystem::path config = std::filesystem::current_path();
    switch (argc)
    {
    case 2:
        config = argv[1];
        [[fallthrough]];
    default:
        std::cout << "config file path: " << config << std::endl;
    }
    Configer::getInstance().load(config);

    // Logger::getInstance().setSuppressLevel(LEVEL::INFO);

    LOG_TRACE("mylog {}", 1);
    LOG_DEBUG("mylog {}", 2);
    LOG_INFO("mylog {}", 3);
    LOG_WARN("mylog {}", 4);
    LOG_ERROR("mylog {}", 5);
    LOG_FATAL("mylog {}", 6);
}