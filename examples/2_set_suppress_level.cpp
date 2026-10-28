#include "example_config.hpp"
#include "logger.hpp"
#include "lua_config.hpp"
#include <iostream>

/**
 * @file 2_set_suppress_level.cpp
 * @brief 日志级别动态抑制与 Lua 配置加载示例
 *
 * 演示：
 * 1. 从外部沙箱 Lua 脚本动态加载配置（原子全量生效）；
 * 2. 通过 C++ API 在运行期动态调整日志抑制门限（支持反射字符串与枚举常量）；
 * 3. 亚纳秒级日志过滤效果（未激活级别的日志直接快速丢弃）。
 */

int main(int argc, char *argv[])
{
    // 1. 探测并加载 Lua 配置文件
    const std::filesystem::path config = mylog_example::resolveConfigPath(argc, argv);
    std::cout << "[example] 加载配置文件: " << config << std::endl;
    if (!mylog::reloadConfig(config))
    {
        std::cerr << "[example] 配置加载失败，沿用当前默认配置" << std::endl;
    }

    mylog::info("配置文件加载后的当前抑制级别: {}", mylog::getCurrentSuppressLevel());

    // 2. 使用 C++ API 动态设置抑制级别
    // 方式 A：传入不区分大小写的枚举字符串（利用 C++26 静态反射自动解析）
    mylog::setSuppressLevel("warn");
    mylog::info("这条 INFO 日志将被亚纳秒级过滤（不会输出）");
    mylog::warn("这是一条 WARN 日志（成功输出）");

    // 方式 B：传入类型安全的枚举常量
    mylog::setSuppressLevel(mylog::LEVEL::TRACE);
    mylog::trace("重置抑制级别为 TRACE 后，TRACE 级别日志正常显示: id={}", 42);

    // 3. 现代化无宏接口调用
    mylog::debug("调试信息输出");
    mylog::info("系统就绪完毕");

    return 0;
}
