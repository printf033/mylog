#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <iostream>
#include <chrono>

enum class LEVEL
{
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

using OutputFunction = std::function<void(std::string_view msg)>;

using FlushFunction = std::function<void(void)>;

using json = nlohmann::json;

// --- 针对 std::chrono::seconds 的转换 ---
namespace std::chrono
{
    void from_json(const json &j, seconds &s)
    {
        s = seconds(j.get<long long>());
    }
}

// --- 针对 LEVEL 枚举的转换 (假设你定义了 LEVEL) ---
// 如果 LEVEL 是 enum class，需要指定映射

NLOHMANN_JSON_SERIALIZE_ENUM(LEVEL, {
                                        {LEVEL::TRACE, "TRACE"},
                                        {LEVEL::DEBUG, "DEBUG"},
                                        {LEVEL::INFO, "INFO"},
                                        {LEVEL::WARN, "WARN"},
                                        {LEVEL::ERROR, "ERROR"},
                                        {LEVEL::FATAL, "FATAL"},
                                    })


// --- 针对各个 Options 的映射 ---

inline void from_json(const json& j, OptionsLogger& o) {
    if (j.contains("suppressLevel")) o.suppressLevel = j.at("suppressLevel").get<LEVEL>();
    // 注意：lv2str 和 function 对象通常不从 JSON 初始化，保持默认即可
}

inline void from_json(const json& j, OptionsFileManager& o) {
    if (j.contains("fileBufCapa")) j.at("fileBufCapa").get_to(o.fileBufCapa);
    if (j.contains("fileCapa"))    j.at("fileCapa").get_to(o.fileCapa);
    if (j.contains("basename"))    o.basename = j.at("basename").get<std::string>().c_str(); 
    // 注意：basename 是 const char*，需要注意生命周期，建议结构体改用 std::string
    if (j.contains("intervalFlushFile")) j.at("intervalFlushFile").get_to(o.intervalFlushFile);
}

inline void from_json(const json& j, OptionsMultiBuffer& o) {
    if (j.contains("bufCapa"))             j.at("bufCapa").get_to(o.bufCapa);
    if (j.contains("intervalFlushBuffer")) j.at("intervalFlushBuffer").get_to(o.intervalFlushBuffer);
}

struct OptionsLogger
{
    LEVEL suppressLevel = LEVEL::TRACE;
    const char *const *lv2str = LV2STR.data();
    OutputFunction outputFunction = [](std::string_view msg)
    { std::cout << msg; };
    FlushFunction flushFunction = []()
    { std::cout.flush(); };

    static constexpr std::array<const char *, 6> LV2STR =
        {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};
    static constexpr std::array<const char *, 6> LV2STR_vivid =
        {"\033[7;37mTRACE\033[0m",
         "\033[7;36mDEBUG\033[0m",
         "\033[7;32mINFO \033[0m",
         "\033[7;33mWARN \033[0m",
         "\033[7;31mERROR\033[0m",
         "\033[5;41mFATAL\033[0m"};
};

struct OptionsFileManager
{
    size_t fileBufCapa = 1048576UL; // 1M
    size_t fileCapa = 104857600UL;  // 100M
    std::string basename = "mylog";
    std::chrono::seconds intervalFlushFile{10};
};

struct OptionsMultiBuffer
{
    size_t bufCapa = 1048576UL; // 1M
    std::chrono::seconds intervalFlushBuffer{1};
};

#include <fstream>

class Configer {
public:
    OptionsLogger loggerOpt;
    OptionsFileManager fileOpt;
    OptionsMultiBuffer bufferOpt;

    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;

        try {
            json data = json::parse(f);

            if (data.contains("logger")) data.at("logger").get_to(loggerOpt);
            if (data.contains("file"))   data.at("file").get_to(fileOpt);
            if (data.contains("buffer")) data.at("buffer").get_to(bufferOpt);
            
            return true;
        } catch (json::exception& e) {
            std::cerr << "Config parse error: " << e.what() << std::endl;
            return false;
        }
    }
};