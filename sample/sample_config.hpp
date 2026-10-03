#pragma once

// 样例共用的配置文件定位逻辑。
//
// 产物现在放在各自构建树的 bin/ 下，样例可能从多个位置启动，
// 因此按常见相对路径依次探测，而不是硬编码 "../sample/config.lua"。
// 显式给出路径时（argv[1]）以命令行参数为准。

#include <filesystem>
#include <string_view>

namespace mylog_sample
{
inline std::filesystem::path resolveConfigPath(int argc, char *argv[])
{
    if (argc > 1 && argv[1] != nullptr && std::string_view(argv[1]).size() > 0)
        return argv[1];

    const std::filesystem::path candidates[] = {
        "config.lua",                // 与二进制同目录放置的配置
        "../sample/config.lua",      // 从构建树根运行
        "../../sample/config.lua",   // 从构建树 bin/ 运行
        "sample/config.lua",         // 从源码树根运行
    };
    std::error_code ec;
    for (const std::filesystem::path &candidate : candidates)
    {
        if (std::filesystem::is_regular_file(candidate, ec))
            return candidate;
    }
    return candidates[1]; // 都没找到：返回惯用路径，让 reload 报出明确错误
}
} // namespace mylog_sample
