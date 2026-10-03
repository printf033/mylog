#pragma once

// 可选的 Lua 配置加载。
//
// 设计约束：
//   * 核心层（logger.hpp / fileManager.hpp）不依赖任何第三方库，本文件是显式包含的可选组件。
//   * 只创建裸 lua_State，**不**调用 luaL_openlibs：配置脚本因此拿不到
//     io / os / package 等标准库，无法读文件、起进程或执行命令。
//   * 解析结果先全部校验、钳制，再整体发布（all-or-nothing），
//     避免热重载过程中出现新旧字段混合的配置。
//
// 结构：两个解析段（logger / fileManager）各自独立，reload() 只负责
// 建 state、取表、汇总校验、发布。

#include "fileManager.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace mylog
{
    namespace detail
    {
        // 以下 read*Field 在字段缺失或类型不符时返回 false 并保持 out 不变
        inline bool readNumberField(lua_State *L, int tableIdx, const char *key,
                                    lua_Integer &out)
        {
            lua_getfield(L, tableIdx, key);
            const bool ok = lua_type(L, -1) == LUA_TNUMBER;
            if (ok)
                out = lua_tointeger(L, -1);
            lua_pop(L, 1);
            return ok;
        }

        inline bool readBoolField(lua_State *L, int tableIdx, const char *key, bool &out)
        {
            lua_getfield(L, tableIdx, key);
            const bool ok = lua_type(L, -1) == LUA_TBOOLEAN;
            if (ok)
                out = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            return ok;
        }

        inline bool readStringField(lua_State *L, int tableIdx, const char *key,
                                    std::string &out)
        {
            lua_getfield(L, tableIdx, key);
            const bool ok = lua_type(L, -1) == LUA_TSTRING;
            if (ok)
            {
                size_t len = 0;
                const char *str = lua_tolstring(L, -1, &len);
                if (str != nullptr)
                    out.assign(str, len);
            }
            lua_pop(L, 1);
            return ok;
        }

        inline void configError(const char *fmt, const char *detailStr = nullptr)
        {
            std::fprintf(stderr, "[mylog] config error: ");
            std::fprintf(stderr, fmt, detailStr);
            std::fprintf(stderr, "\n");
        }

        // —— logger 段：把脚本明确给出的字段覆盖到 cfg 上 ——
        // 返回 false 表示出现了非法值（调用方据此整体拒绝本次加载）。
        inline bool loadLoggerSection(lua_State *L, int idx, Logger::Config &cfg,
                                      LEVEL &suppressLevel)
        {
            bool valid = true;
            lua_Integer num = 0;
            bool flag = false;
            std::string text;

            if (readNumberField(L, idx, "suppressLevel", num))
            {
                if (num < 0 || num >= static_cast<lua_Integer>(kLevelCount))
                {
                    configError("logger.suppressLevel out of range: %s", "");
                    valid = false;
                }
                else
                {
                    suppressLevel = static_cast<LEVEL>(num);
                }
            }
            if (readBoolField(L, idx, "stdoutVivid", flag))
                cfg.lv2str = flag ? &LV2STR_VIVID : &LV2STR;
            if (readBoolField(L, idx, "escapeControlChars", flag))
                cfg.escapeControlChars = flag;
            if (readBoolField(L, idx, "timestampWithMs", flag))
                cfg.timestampWithMs = flag;
            if (readBoolField(L, idx, "timestampUtc", flag))
                cfg.timestampUtc = flag;
            // 单条日志字节上限：超出即截断
            if (readNumberField(L, idx, "maxLogLength", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(
                    num, static_cast<lua_Integer>(kMinMaxLogLength),
                    static_cast<lua_Integer>(kMaxMaxLogLength));
                if (v != num)
                    configError("%s", "logger.maxLogLength clamped");
                cfg.maxLogLength = static_cast<size_t>(v);
            }

            if (readStringField(L, idx, "fatalAction", text))
            {
                if (text == "abort")
                    cfg.fatalAction = FATAL_ACTION::ABORT;
                else if (text == "exit")
                    cfg.fatalAction = FATAL_ACTION::EXIT;
                else if (text == "none")
                    cfg.fatalAction = FATAL_ACTION::NONE;
                else
                {
                    configError("logger.fatalAction unknown value: %s", text.c_str());
                    valid = false;
                }
            }
            else if (readNumberField(L, idx, "fatalAction", num))
            {
                if (num < 0 || num > 2)
                {
                    configError("%s", "logger.fatalAction out of range");
                    valid = false;
                }
                else
                {
                    cfg.fatalAction = static_cast<FATAL_ACTION>(num);
                }
            }
            return valid;
        }

        // —— fileManager 段：同上 ——
        inline bool loadFileManagerSection(lua_State *L, int idx, FileManager::Config &cfg)
        {
            bool valid = true;
            lua_Integer num = 0;
            std::string text;

            // 每线程缓冲上限：0 表示关闭（退回单锁直写）
            if (readNumberField(L, idx, "threadBufferCapacity", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(
                    num, 0, static_cast<lua_Integer>(kMaxThreadBufferCapacity));
                if (v != num)
                    configError("%s", "fileManager.threadBufferCapacity clamped");
                cfg.threadBufferCapacity = static_cast<size_t>(v);
            }
            if (readNumberField(L, idx, "fileBufferCapacity", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(
                    num, static_cast<lua_Integer>(kMinFileBufferCapacity),
                    static_cast<lua_Integer>(kMaxFileBufferCapacity));
                if (v != num)
                    configError("%s", "fileManager.fileBufferCapacity clamped");
                cfg.fileBufferCapacity = static_cast<size_t>(v);
            }
            if (readNumberField(L, idx, "fileCapacity", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(
                    num, static_cast<lua_Integer>(kMinFileCapacity),
                    static_cast<lua_Integer>(kMaxFileCapacity));
                if (v != num)
                    configError("%s", "fileManager.fileCapacity clamped");
                cfg.fileCapacity = static_cast<size_t>(v);
            }
            // intervalFlushFile_ms 优先；intervalFlushFile 以秒为单位（兼容旧配置）
            if (readNumberField(L, idx, "intervalFlushFile_ms", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(
                    num, 0, static_cast<lua_Integer>(kMaxIntervalFlushMs));
                if (v != num)
                    configError("%s", "fileManager.intervalFlushFile_ms clamped");
                cfg.intervalFlushFile_ms = static_cast<size_t>(v);
            }
            else if (readNumberField(L, idx, "intervalFlushFile", num))
            {
                const lua_Integer seconds = clampValue<lua_Integer>(
                    num, 0, static_cast<lua_Integer>(kMaxIntervalFlushMs / 1000));
                if (seconds != num)
                    configError("%s", "fileManager.intervalFlushFile clamped");
                cfg.intervalFlushFile_ms = static_cast<size_t>(seconds) * 1000;
            }
            if (readNumberField(L, idx, "maxFiles", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(
                    num, 0, static_cast<lua_Integer>(kMaxMaxFiles));
                if (v != num)
                    configError("%s", "fileManager.maxFiles clamped");
                cfg.maxFiles = static_cast<size_t>(v);
            }
            if (readStringField(L, idx, "basename", text))
            {
                if (FileManager::isValidBasename(text))
                    cfg.basename = text;
                else
                {
                    configError("fileManager.basename rejected: %s", text.c_str());
                    valid = false;
                }
            }
            return valid;
        }
    } // namespace detail

    // 加载 Lua 配置：校验通过后整体发布（all-or-nothing），任一项非法则保持旧配置。
    // 直接调用两个单例（Logger / FileManager）的公开 API。
    inline bool reloadConfig(const std::filesystem::path &config)
    {
        // 裸 state：脚本没有 io/os/package，不具备 I/O、进程或调试能力
        lua_State *L = luaL_newstate();
        if (L == nullptr)
        {
            detail::configError("%s", "cannot create lua state");
            return false;
        }

        if (luaL_dofile(L, config.c_str()) != LUA_OK)
        {
            const char *err = lua_tostring(L, -1);
            detail::configError("%s", err != nullptr ? err : "lua load failed");
            lua_close(L);
            return false;
        }

        // 以当前生效配置为基础，只覆盖脚本明确给出的字段
        Logger::Config loggerCfg = Logger::getInstance().getConfig();
        FileManager::Config fileCfg = FileManager::getInstance().getConfig();
        LEVEL suppressLevel = Logger::getInstance().getSuppressLevel();
        bool valid = true;

        lua_getglobal(L, "logger");
        if (lua_istable(L, -1))
        {
            const int idx = lua_gettop(L);
            if (!detail::loadLoggerSection(L, idx, loggerCfg, suppressLevel))
                valid = false;
        }
        lua_pop(L, 1);

        lua_getglobal(L, "fileManager");
        if (lua_istable(L, -1))
        {
            const int idx = lua_gettop(L);
            if (!detail::loadFileManagerSection(L, idx, fileCfg))
                valid = false;
        }
        lua_pop(L, 1);

        lua_close(L);

        if (!valid)
            return false; // 有任何一项非法就整体拒绝，保持旧配置生效

        // 全部字段校验通过后才发布，避免半套配置生效
        Logger::getInstance().setConfig(std::move(loggerCfg));
        FileManager::getInstance().setConfig(std::move(fileCfg));
        Logger::getInstance().setSuppressLevel(suppressLevel);
        return true;
    }

} // namespace mylog
