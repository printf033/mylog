add_library(mylog INTERFACE)

target_include_directories(mylog INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(mylog INTERFACE
    cxx_std_26
)

target_compile_options(mylog INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -fmacro-prefix-map=${PROJECT_SOURCE_DIR}/=>
    $<$<CXX_COMPILER_ID:GNU>:-freflection>
)

find_package(Threads REQUIRED)
target_link_libraries(mylog INTERFACE Threads::Threads)

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

find_package(PkgConfig REQUIRED)
pkg_check_modules(LUAJIT REQUIRED IMPORTED_TARGET luajit)

add_library(mylog_lua INTERFACE)
target_link_libraries(mylog_lua INTERFACE mylog PkgConfig::LUAJIT)
