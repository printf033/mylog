-- mylog 配置示例（由 Configer::reload("config.lua") 载入）
--
-- 安全说明：脚本在裸 lua_State 中执行，io / os / package / debug 等标准库
-- 都没有注册，因此配置脚本无法读写文件、启动进程或执行命令。
-- 字段类型不符会被忽略，数值越界会被钳制，非法 basename 会让整份配置被拒绝。

logger = {
    -- 低于该级别的日志被直接丢弃
    -- TRACE = 0
    -- DEBUG = 1
    -- INFO = 2
    -- WARN = 3
    -- ERROR = 4
    -- FATAL = 5
    suppressLevel = 0,

    -- true 时级别标签带 ANSI 颜色；只适合终端，写文件请用 false
    stdoutVivid = false,

    -- 是否转义控制字符（含 ESC / \x7f），用于阻断日志注入伪造 ANSI 序列
    -- 默认 true；只有输出目标完全可信时才建议关闭
    escapeControlChars = true,

    -- 时间戳是否带毫秒。false 走 thread_local 秒级缓存，热路径更快
    timestampWithMs = true,

    -- 时间戳用 UTC（true）还是本地时间（false，默认）
    timestampUtc = false,

    -- 单条日志字节上限，超出部分被截断
    -- 合法区间 [64, 64 MiB]，默认 1 MiB
    maxLogLength = 1048576,

    -- FATAL 触发后的动作："none" | "exit" | "abort"（默认 "abort"）
    -- "none" 适合嵌入式/测试场景：日志库不替宿主进程决定生死
    fatalAction = "none"
}

fileManager = {
    -- 每线程私有写缓冲（字节），0 = 关闭并退回「全局锁 + 共享缓冲」
    -- 多线程写同一个文件时收益明显；关掉（设 0）会明显变慢
    threadBufferCapacity = 65536,

    -- 共享写缓冲（字节），线程缓冲关闭或排空到共享文件时使用
    fileBufferCapacity = 131072,

    -- 单个日志文件上限（字节），超过即滚动到新文件
    -- 合法区间 [1024, 64 GiB]
    fileCapacity = 104857600,

    -- 距上次写盘超过该毫秒数就 flush
    -- 合法区间 [0, 24h]；也兼容旧的秒单位键 intervalFlushFile
    intervalFlushFile_ms = 30000,

    -- 保留的日志文件数上限，0 = 不清理（需显式写 0），上限 1024
    -- 默认 5：磁盘占用上界 ≈ maxFiles × fileCapacity，避免长期运行写满磁盘
    maxFiles = 5,

    -- 日志文件名前缀，只允许 [A-Za-z0-9._-]，长度 <= 128
    -- 含路径分隔符、空白或路径穿越（"." / ".."）会被拒绝
    basename = "default"
}
