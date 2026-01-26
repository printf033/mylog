#include "logger.hpp"
#include "fileManager.hpp"

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
    
    Logger::getInstance().setOutputFunction(FileManager::outputFunction_file);
    Logger::getInstance().setFlushFunction(FileManager::flushFunction_file);

    LOG_TRACE("mylog {}", "trace");
    LOG_DEBUG("mylog {}", "debug");
    LOG_INFO("mylog {}", "info");
    LOG_WARN("mylog {}", "warn");
    LOG_ERROR("mylog {}", "error");
    LOG_FATAL("mylog {}", "fatal");
}