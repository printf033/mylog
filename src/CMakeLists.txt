add_library(interface_mylog INTERFACE)

target_include_directories(interface_mylog INTERFACE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(interface_mylog INTERFACE
    cxx_std_20
)

target_compile_options(interface_mylog INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=>
)

find_package(Threads REQUIRED)
target_link_libraries(interface_mylog INTERFACE Threads::Threads)

# Debug 构建注入 ASan/UBSan。单独成一个 INTERFACE 目标（而不是塞进
# interface_mylog），这样基准测试可以不链接它 —— 否则测量的是插桩开销。
option(MYLOG_SANITIZE "Enable ASan/UBSan for Debug builds" ON)

add_library(mylog_sanitized INTERFACE)

if(MYLOG_SANITIZE)
    target_compile_options(mylog_sanitized INTERFACE
        $<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-fsanitize=address,undefined -fno-omit-frame-pointer>
    )
    target_link_options(mylog_sanitized INTERFACE
        $<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-fsanitize=address,undefined>
    )
endif()

# 核心层零第三方依赖：linker 不需要 Lua。
#
# Lua 配置解析是可选组件（configer_lua.hpp）。只有真正包含它的目标才链接
# LuaJIT，从而让不使用 Lua 的用户完全不引入该依赖。
find_package(PkgConfig REQUIRED)
pkg_check_modules(LUAJIT REQUIRED IMPORTED_TARGET luajit)

add_library(mylog_lua INTERFACE)
target_link_libraries(mylog_lua INTERFACE interface_mylog PkgConfig::LUAJIT)
