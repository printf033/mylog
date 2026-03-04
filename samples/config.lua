logger = {
    -- TRACE = 0
    -- DEBUG = 1
    -- INFO = 2
    -- WARN = 3
    -- ERROR = 4
    -- FATAL = 5
    suppressLevel = 0,
    -- true for stdout
    stdoutVivid = false
}

fileManager = {
    -- 2M
    fileBufferCapacity = 2097152,
    -- 100M
    fileCapacity = 104857600,
    -- log file base name
    basename = "mylog",
    -- s
    intervalFlushFile = 30
}

multiBuffer = {
    -- 1M
    preBufferCapacity = 1048576,
    -- s
    intervalFlushBuffer = 5
}
