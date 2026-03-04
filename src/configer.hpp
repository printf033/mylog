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

#include <string>

using OutputFunction = void (*)(std::string_view msg);

using FlushFunction = void (*)();

#include <iostream>
#include <array>

struct OptionsLogger
{
    LEVEL suppressLevel = LEVEL::TRACE;        // trace
    const char *const *lv2str = LV2STR.data(); // plain
    OutputFunction outputFunction = [](std::string_view msg)
    { std::cout << msg; }; // stdout
    FlushFunction flushFunction = []()
    { std::cout.flush(); }; // stdout

    static constexpr std::array<const char *, 6> LV2STR =
        {"TRACE",
         "DEBUG",
         "INFO ",
         "WARN ",
         "ERROR",
         "FATAL"};
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
    size_t fileBufferCapacity = 2097152UL;      // 2M
    size_t fileCapacity = 104857600UL;          // 100M
    std::string basename = "mylog";             // default
    std::chrono::seconds intervalFlushFile{30}; // 30s
};

struct OptionsMultiBuffer
{
    size_t preBufferCapacity = 1048576UL;        // 1M
    std::chrono::seconds intervalFlushBuffer{5}; // 5s
};

#include <filesystem>
extern "C"
{
#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>
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
        lua_State *luaState = luaL_newstate();
        luaL_openlibs(luaState);
        if (luaL_dofile(luaState, config.c_str()) != LUA_OK)
        {
            std::cerr << lua_tostring(luaState, -1) << std::endl;
            lua_close(luaState);
            return false;
        }

        if (lua_getglobal(luaState, "logger") == LUA_TTABLE)
        {
            if (lua_getfield(luaState, -1, "suppressLevel") == LUA_TNUMBER)
            {
                optLogger.suppressLevel = static_cast<LEVEL>(lua_tointeger(luaState, -1));
            }
            lua_pop(luaState, 1);

            if (lua_getfield(luaState, -1, "stdoutVivid") == LUA_TBOOLEAN)
            {
                if (lua_toboolean(luaState, -1))
                {
                    optLogger.lv2str = optLogger.LV2STR_vivid.data();
                }
                else
                {
                    optLogger.lv2str = optLogger.LV2STR.data();
                }
            }
            lua_pop(luaState, 1);
        }

        if (lua_getglobal(luaState, "fileManager") == LUA_TTABLE)
        {
            if (lua_getfield(luaState, -1, "fileBufferCapacity") == LUA_TNUMBER)
            {
                optFileManager.fileBufferCapacity = lua_tointeger(luaState, -1);
            }
            lua_pop(luaState, 1);

            if (lua_getfield(luaState, -1, "fileCapacity") == LUA_TNUMBER)
            {
                optFileManager.fileCapacity = lua_tointeger(luaState, -1);
            }
            lua_pop(luaState, 1);

            if (lua_getfield(luaState, -1, "basename") == LUA_TSTRING)
            {
                optFileManager.basename = lua_tostring(luaState, -1);
            }
            lua_pop(luaState, 1);

            if (lua_getfield(luaState, -1, "intervalFlushFile") == LUA_TNUMBER)
            {
                optFileManager.intervalFlushFile = std::chrono::seconds(lua_tointeger(luaState, -1));
            }
            lua_pop(luaState, 1);
        }

        if (lua_getglobal(luaState, "multiBuffer") == LUA_TTABLE)
        {
            if (lua_getfield(luaState, -1, "preBufferCapacity") == LUA_TNUMBER)
            {
                optMultiBuffer.preBufferCapacity = lua_tointeger(luaState, -1);
            }
            lua_pop(luaState, 1);

            if (lua_getfield(luaState, -1, "intervalFlushBuffer") == LUA_TNUMBER)
            {
                optMultiBuffer.intervalFlushBuffer = std::chrono::seconds(lua_tointeger(luaState, -1));
            }
            lua_pop(luaState, 1);
        }

        lua_close(luaState);
        return true;
    }

private:
    Configer() noexcept = default;
    ~Configer() noexcept = default;
    Configer(const Configer &) = delete;
    Configer &operator=(const Configer &) = delete;
    Configer(Configer &&) noexcept = delete;
    Configer &operator=(Configer &&) noexcept = delete;
};
