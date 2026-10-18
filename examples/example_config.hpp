#pragma once

#include <filesystem>
#include <string_view>

namespace mylog_example
{
    /**
     * @brief 探测并解析样例程序使用的 Lua 配置文件路径
     *
     * 依次在命令行参数、当前工作目录、上层目录及 examples/ 目录下探测 config.lua，
     * 避免因运行工作目录不同（如从源码根目录、build/ 或 build/bin/ 启动）导致找不到配置。
     *
     * @param argc 命令行参数个数
     * @param argv 命令行参数字符串数组
     * @return std::filesystem::path 探测到的有效配置文件路径或推荐候选路径
     */
    inline std::filesystem::path resolveConfigPath(int argc, char *argv[])
    {
        if (argc > 1 && argv[1] != nullptr && std::string_view(argv[1]).size() > 0)
            return argv[1];

        const std::filesystem::path candidates[] = {
            "config.lua",                // 与二进制同目录放置的配置
            "../examples/config.lua",    // 从构建树根运行
            "../../examples/config.lua", // 从构建树 bin/ 运行
            "examples/config.lua",       // 从源码树根运行
            "../example/config.lua",     // 兼容历史路径
            "../../example/config.lua",
            "example/config.lua",
        };
        std::error_code ec;
        for (const std::filesystem::path &candidate : candidates)
        {
            if (std::filesystem::is_regular_file(candidate, ec))
                return candidate;
        }
        return candidates[1];
    }
} // namespace mylog_example

namespace mylog_sample = mylog_example;
