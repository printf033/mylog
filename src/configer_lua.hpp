#pragma once

#include "fileManager.hpp"
#include <cstdio>
#include <filesystem>
#include <format>
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
        /**
         * @brief 从 Lua 表中读取整数型字段
         *
         * 从指定索引的 Lua table 中获取键为 key 的值；若为数字类型则提取为整型。
         *
         * @param L Lua 状态机指针
         * @param tableIdx 表在虚拟栈中的索引
         * @param key 字段键名
         * @param out 输出接收整数变量
         * @return true 字段存在且类型为数字
         * @return false 字段不存在或类型不匹配
         */
        inline bool readNumberField(lua_State *L, int tableIdx, const char *key, lua_Integer &out)
        {
            lua_getfield(L, tableIdx, key);
            const bool ok = lua_type(L, -1) == LUA_TNUMBER;
            if (ok)
                out = lua_tointeger(L, -1);
            lua_pop(L, 1);
            return ok;
        }

        /**
         * @brief 从 Lua 表中读取布尔型字段
         *
         * 从指定索引的 Lua table 中获取键为 key 的值；若为布尔类型则提取。
         *
         * @param L Lua 状态机指针
         * @param tableIdx 表在虚拟栈中的索引
         * @param key 字段键名
         * @param out 输出接收布尔变量
         * @return true 字段存在且类型为布尔
         * @return false 字段不存在或类型不匹配
         */
        inline bool readBoolField(lua_State *L, int tableIdx, const char *key, bool &out)
        {
            lua_getfield(L, tableIdx, key);
            const bool ok = lua_type(L, -1) == LUA_TBOOLEAN;
            if (ok)
                out = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            return ok;
        }

        /**
         * @brief 从 Lua 表中读取字符串字段
         *
         * 从指定索引的 Lua table 中获取键为 key 的值；若为字符串类型则赋值至 std::string。
         *
         * @param L Lua 状态机指针
         * @param tableIdx 表在虚拟栈中的索引
         * @param key 字段键名
         * @param out 输出接收字符串变量
         * @return true 字段存在且类型为字符串
         * @return false 字段不存在或类型不匹配
         */
        inline bool readStringField(lua_State *L, int tableIdx, const char *key, std::string &out)
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

        /**
         * @brief 输出配置解析警告或错误信息至标准错误
         *
         * 基于 std::format 而非 printf 风格，避免调用点误传非常量格式串导致
         * 格式化字符串注入风险；detailStr 作为位置参数安全插入。
         *
         * @param fmt std::format 格式化字符串（编译期检查）
         * @param detailStr 可选的具体错误明细字符串
         */
        template <typename... Args>
        inline void printError(std::format_string<Args...> fmt, Args &&...args)
        {
            std::fprintf(stderr, "[mylog] config error: %s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
        }

        /// 无参数占位重载：保持 "无明细" 调用点可读（format 串无占位符时直接输出）
        inline void printError(std::format_string<> fmt)
        {
            std::fprintf(stderr, "[mylog] config error: %s\n", std::format(fmt).c_str());
        }

        /**
         * @brief 解析并加载 Lua 配置中的 logger 配置节
         *
         * 从 Lua 栈中的 logger table 提取并校验各字段配置：
         * 包括 suppressLevel、stdoutVivid、escapeControlChars、
         * timestampUtc、maxLogLength 以及 fatalAction。时间戳精度统一为纳秒级无需开关。
         *
         * @param L Lua 状态机指针
         * @param idx logger 表在栈上的索引
         * @param cfg 待填充的 Logger::Config 配置结构体
         * @param suppressLevel 待更新的日志抑制级别变量
         * @return true 所有字段合法或已容错纠偏
         * @return false 存在无法解析的关键配置项（如未知枚举值或非法类型）
         */
        inline bool loadLoggerSection(lua_State *L, int idx, Logger::Config &cfg, LEVEL &suppressLevel)
        {
            bool valid = true;
            lua_Integer num = 0;
            bool flag = false;
            std::string text;

            if (readStringField(L, idx, "suppressLevel", text))
            {
                if (auto lvl = parseLevel(text))
                {
                    suppressLevel = *lvl;
                }
                else
                {
                    printError("logger.suppressLevel unknown value: {}", text.c_str());
                    valid = false;
                }
            }
            else if (readNumberField(L, idx, "suppressLevel", num))
            {
                printError("logger.suppressLevel must be a level string, integer not allowed");
                valid = false;
            }
            if (readBoolField(L, idx, "stdoutVivid", flag))
                cfg.lv2str = flag ? &LV2STR_VIVID : &LV2STR;
            if (readBoolField(L, idx, "escapeControlChars", flag))
                cfg.escapeControlChars = flag;
            if (readBoolField(L, idx, "timestampUtc", flag))
                cfg.timestampUtc = flag;
            if (readNumberField(L, idx, "maxLogLength", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(num, static_cast<lua_Integer>(kMinMaxLogLength),
                                                              static_cast<lua_Integer>(kMaxMaxLogLength));
                if (v != num)
                    printError("logger.maxLogLength clamped");
                cfg.maxLogLength = static_cast<size_t>(v);
            }

            if (readStringField(L, idx, "fatalAction", text))
            {
                if (auto act = parseFatalAction(text))
                {
                    cfg.fatalAction = *act;
                }
                else
                {
                    printError("logger.fatalAction unknown value: {}", text.c_str());
                    valid = false;
                }
            }
            else if (readNumberField(L, idx, "fatalAction", num))
            {
                printError("logger.fatalAction must be a string, integer not allowed");
                valid = false;
            }
            return valid;
        }

        /**
         * @brief 基于 C++26 静态反射自动解析并校验 fileManager 配置节
         *
         * 遍历 FileManager::Config 的所有成员变量元信息，按字段名自动从 Lua 表中提取
         * 并依据预设安全范围进行钳位（clamp）或校验，无需手写繁琐的逐字段映射。
         *
         * @param L Lua 状态机指针
         * @param idx fileManager 表在栈上的索引
         * @param cfg 待填充的 FileManager::Config 配置结构体
         * @return true 所有字段合法并已安全钳位
         */
        inline bool loadFileManagerSection(lua_State *L, int idx, FileManager::Config &cfg)
        {
            bool valid = true;
            lua_Integer num = 0;

#if MYLOG_HAS_REFLECTION
            static constexpr auto members = reflect::getMemberInfos<FileManager::Config>();
            template for (constexpr auto m : members)
            {
                constexpr auto fieldName = std::meta::identifier_of(m);
                using FieldType = [:std::meta::type_of(m):];

                if constexpr (std::is_same_v<FieldType, size_t>)
                {
                    if (readNumberField(L, idx, fieldName.data(), num))
                    {
                        if constexpr (fieldName == "threadBufferCapacity")
                        {
                            const lua_Integer v =
                                clampValue<lua_Integer>(num, 0, static_cast<lua_Integer>(kMaxThreadBufferCapacity));
                            if (v != num)
                                printError("fileManager.threadBufferCapacity clamped");
                            cfg.[:m:] = static_cast<size_t>(v);
                        }
                        else if constexpr (fieldName == "fileBufferCapacity")
                        {
                            const lua_Integer v =
                                clampValue<lua_Integer>(num, static_cast<lua_Integer>(kMinFileBufferCapacity),
                                                        static_cast<lua_Integer>(kMaxFileBufferCapacity));
                            if (v != num)
                                printError("fileManager.fileBufferCapacity clamped");
                            cfg.[:m:] = static_cast<size_t>(v);
                        }
                        else if constexpr (fieldName == "fileCapacity")
                        {
                            const lua_Integer v =
                                clampValue<lua_Integer>(num, static_cast<lua_Integer>(kMinFileCapacity),
                                                        static_cast<lua_Integer>(kMaxFileCapacity));
                            if (v != num)
                                printError("fileManager.fileCapacity clamped");
                            cfg.[:m:] = static_cast<size_t>(v);
                        }
                        else if constexpr (fieldName == "intervalFlushFile_ms")
                        {
                            const lua_Integer v =
                                clampValue<lua_Integer>(num, 0, static_cast<lua_Integer>(kMaxIntervalFlushMs));
                            if (v != num)
                                printError("fileManager.intervalFlushFile_ms clamped");
                            cfg.[:m:] = static_cast<size_t>(v);
                        }
                        else if constexpr (fieldName == "maxFiles")
                        {
                            const lua_Integer v =
                                clampValue<lua_Integer>(num, 0, static_cast<lua_Integer>(kMaxMaxFiles));
                            if (v != num)
                                printError("fileManager.maxFiles clamped");
                            cfg.[:m:] = static_cast<size_t>(v);
                        }
                    }
                }
            }
#else
            if (readNumberField(L, idx, "threadBufferCapacity", num))
            {
                const lua_Integer v =
                    clampValue<lua_Integer>(num, 0, static_cast<lua_Integer>(kMaxThreadBufferCapacity));
                if (v != num)
                    printError("fileManager.threadBufferCapacity clamped");
                cfg.threadBufferCapacity = static_cast<size_t>(v);
            }
            if (readNumberField(L, idx, "fileBufferCapacity", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(num, static_cast<lua_Integer>(kMinFileBufferCapacity),
                                                              static_cast<lua_Integer>(kMaxFileBufferCapacity));
                if (v != num)
                    printError("fileManager.fileBufferCapacity clamped");
                cfg.fileBufferCapacity = static_cast<size_t>(v);
            }
            if (readNumberField(L, idx, "fileCapacity", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(num, static_cast<lua_Integer>(kMinFileCapacity),
                                                              static_cast<lua_Integer>(kMaxFileCapacity));
                if (v != num)
                    printError("fileManager.fileCapacity clamped");
                cfg.fileCapacity = static_cast<size_t>(v);
            }
            if (readNumberField(L, idx, "intervalFlushFile_ms", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(num, 0, static_cast<lua_Integer>(kMaxIntervalFlushMs));
                if (v != num)
                    printError("fileManager.intervalFlushFile_ms clamped");
                cfg.intervalFlushFile_ms = static_cast<size_t>(v);
            }
            if (readNumberField(L, idx, "maxFiles", num))
            {
                const lua_Integer v = clampValue<lua_Integer>(num, 0, static_cast<lua_Integer>(kMaxMaxFiles));
                if (v != num)
                    printError("fileManager.maxFiles clamped");
                cfg.maxFiles = static_cast<size_t>(v);
            }
#endif

            return valid;
        }
    } // namespace detail

    /**
     * @brief 从 Lua 脚本文件重新加载全局日志系统配置
     *
     * 执行全量原子化配置加载：
     * 1. 创建独立的隔离 Lua 虚拟机并执行目标配置文件；
     * 2. 分别提取 logger 与 fileManager 两个配置节并进行合法性校验与安全钳位；
     * 3. 校验全部通过后，通过原子发布（ConfigHolder::publish）更新 Logger 与 FileManager 单例；
     *    若解析失败则放弃更新并保留当前运行中配置，确保日志服务的高可用与配置原子性。
     *
     * @param config Lua 配置文件所在的路径
     * @return true 配置文件加载且校验成功，新配置已原子生效
     * @return false 配置文件不存在、语法错误或包含无法解析的非法项，已沿用原配置
     */
    inline bool reloadConfig(const std::filesystem::path &config)
    {
        lua_State *L = luaL_newstate();
        if (L == nullptr)
        {
            detail::printError("cannot create lua state");
            return false;
        }

        lua_sethook(
            L, [](lua_State *LS, lua_Debug * /*ar*/)
            { luaL_error(LS, "config script aborted: instruction budget exceeded"); }, LUA_MASKCOUNT, 50'000'000);

        if (luaL_loadfilex(L, config.c_str(), "t") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
        {
            const char *err = lua_tostring(L, -1);
            detail::printError("{}", err != nullptr ? err : "lua load failed");
            lua_close(L);
            return false;
        }

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
            return false;

        Logger::getInstance().setConfig(std::move(loggerCfg));
        FileManager::getInstance().setConfig(std::move(fileCfg));
        Logger::getInstance().setSuppressLevel(suppressLevel);
        return true;
    }

} // namespace mylog
