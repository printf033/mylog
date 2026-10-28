#pragma once

#include <filesystem>
#include <string_view>

namespace mylog_example
{
    /**
     * @brief 探测并解析样例程序使用的 Lua 配置文件路径
     *
     * 依次在命令行参数与当前工作目录下探测 config.lua，兼容从源码树根、build/ 或 build/bin/ 启动，
     * 避免因运行工作目录不同导致找不到配置。
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
            "config.lua",              // 当前工作目录下的独立配置，优先级最高
            "config/config.lua",       // 从源码树根运行
            "../config/config.lua",    // 从构建树根（build/）运行
            "../../config/config.lua", // 从构建树 bin/（build/bin/）运行
        };
        std::error_code ec;
        for (const std::filesystem::path &candidate : candidates)
        {
            if (std::filesystem::is_regular_file(candidate, ec))
                return candidate;
        }
        // 兜底路径仅用于报错提示，取源码树根布局
        return candidates[1];
    }
} // namespace mylog_example

namespace mylog_sample = mylog_example;
