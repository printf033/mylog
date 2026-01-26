#pragma once

enum class LEVEL
{
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

#include <functional>
#include <string>

using OutputFunction = std::function<void(std::string_view msg)>;

using FlushFunction = std::function<void(void)>;

#include <iostream>

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

#include <chrono>

struct OptionsFileManager
{
    size_t fileBufferCapacity = 2097152UL; // 2M
    size_t fileCapacity = 104857600UL;     // 100M
    std::string basename = "mylog";
    std::chrono::seconds intervalFlushFile{30};
};

struct OptionsMultiBuffer
{
    size_t preBufferCapacity = 1048576UL; // 1M
    std::chrono::seconds intervalFlushBuffer{5};
};

#include <fstream>
#include <nlohmann/json.hpp>

namespace std::chrono
{
    void from_json(const nlohmann::json &res, seconds &var)
    {
        var = seconds(res.get<int64_t>());
    }
}

NLOHMANN_JSON_SERIALIZE_ENUM(LEVEL, {
                                        {LEVEL::TRACE, "TRACE"},
                                        {LEVEL::DEBUG, "DEBUG"},
                                        {LEVEL::INFO, "INFO"},
                                        {LEVEL::WARN, "WARN"},
                                        {LEVEL::ERROR, "ERROR"},
                                        {LEVEL::FATAL, "FATAL"},
                                    })
inline void from_json(const nlohmann::json &res, OptionsLogger &var)
{
    if (res.contains("suppressLevel"))
        var.suppressLevel = res.at("suppressLevel").get<LEVEL>();
    if (res.contains("stdoutVivid"))
    {
        if (res.at("stdoutVivid").get<bool>())
        {
            var.lv2str = var.LV2STR_vivid.data();
        }
        else
        {
            var.lv2str = var.LV2STR.data();
        }
    }
}

inline void from_json(const nlohmann::json &res, OptionsFileManager &var)
{
    if (res.contains("fileBufferCapacity"))
        res.at("fileBufferCapacity").get_to(var.fileBufferCapacity);
    if (res.contains("fileCapacity"))
        res.at("fileCapacity").get_to(var.fileCapacity);
    if (res.contains("basename"))
        res.at("basename").get_to(var.basename);
    if (res.contains("intervalFlushFile"))
        res.at("intervalFlushFile").get_to(var.intervalFlushFile);
}

inline void from_json(const nlohmann::json &res, OptionsMultiBuffer &var)
{
    if (res.contains("preBufferCapacity"))
        res.at("preBufferCapacity").get_to(var.preBufferCapacity);
    if (res.contains("intervalFlushBuffer"))
        res.at("intervalFlushBuffer").get_to(var.intervalFlushBuffer);
}

struct Configer
{
    OptionsLogger optLogger;
    OptionsFileManager optFileManager;
    OptionsMultiBuffer optMultiBuffer;

    inline static Configer &getInstance() noexcept
    {
        static Configer instance;
        return instance;
    }
    bool load(const std::filesystem::path &config)
    {
        std::ifstream ifs(config);
        if (!ifs.is_open())
            return false;
        try
        {
            nlohmann::json data = nlohmann::json::parse(ifs);
            if (data.contains("logger"))
                data.at("logger").get_to(optLogger);
            if (data.contains("fileManager"))
                data.at("fileManager").get_to(optFileManager);
            if (data.contains("multiBuffer"))
                data.at("multiBuffer").get_to(optMultiBuffer);
            return true;
        }
        catch (nlohmann::json::exception &e)
        {
            std::cerr << "Config Parse Error: " << e.what() << std::endl;
            return false;
        }
    }

private:
    Configer() noexcept = default;
    ~Configer() noexcept = default;
    Configer(const Configer &) = delete;
    Configer &operator=(const Configer &) = delete;
    Configer(Configer &&) noexcept = delete;
    Configer &operator=(Configer &&) noexcept = delete;
};
