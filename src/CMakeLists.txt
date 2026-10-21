add_library(mylog INTERFACE)
add_library(mylog::mylog ALIAS mylog)

target_include_directories(mylog INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(mylog INTERFACE
    cxx_std_26
)

target_compile_options(mylog INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -fmacro-prefix-map=${mylog_SOURCE_DIR}/=>
    $<$<CXX_COMPILER_ID:GNU>:-freflection>
)

find_package(Threads REQUIRED)
target_link_libraries(mylog INTERFACE Threads::Threads)

# Sanitizer helper target (optional, used in tests and examples)
option(MYLOG_SANITIZE "Enable ASan/UBSan for Debug builds" ${MYLOG_IS_TOP_LEVEL})

add_library(mylog_sanitized INTERFACE)
add_library(mylog::mylog_sanitized ALIAS mylog_sanitized)

if(MYLOG_SANITIZE)
    target_compile_options(mylog_sanitized INTERFACE
        $<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-fsanitize=address,undefined -fno-omit-frame-pointer>
    )
    target_link_options(mylog_sanitized INTERFACE
        $<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-fsanitize=address,undefined>
    )
endif()

# Lua configuration support (optional, requires LuaJIT)
option(MYLOG_ENABLE_LUA "Enable Lua configuration support (requires LuaJIT)" ON)

if(MYLOG_ENABLE_LUA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUAJIT REQUIRED IMPORTED_TARGET luajit)

    add_library(mylog_lua INTERFACE)
    add_library(mylog::mylog_lua ALIAS mylog_lua)
    target_link_libraries(mylog_lua INTERFACE mylog::mylog PkgConfig::LUAJIT)
endif()
