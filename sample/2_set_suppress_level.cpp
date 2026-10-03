#include "logger.hpp"
#include "configer_lua.hpp"
#include "sample_config.hpp"
#include <iostream>

int main(int argc, char *argv[])
{
    const std::filesystem::path config = mylog_sample::resolveConfigPath(argc, argv);
    std::cout << "config file path: " << config << std::endl;
    if (!mylog::reloadConfig(config))
        std::cerr << "配置加载失败，沿用当前配置" << std::endl;

    // 也可以直接用代码设置抑制级别（优先级与 reload 相同，后设置者生效）
    // mylog::Logger::getInstance().setSuppressLevel(mylog::LEVEL::INFO);

    LOG_TRACE("mylog {}", 1);
    LOG_DEBUG("mylog {}", 2);
    LOG_INFO("mylog {}", 3);
    LOG_WARN("mylog {}", 4);
    LOG_ERROR("mylog {}", 5);
    LOG_FATAL("mylog {}", 6);
}
