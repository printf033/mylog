function(gen_bin name)

    add_executable(${name} ${name}.cpp)

    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(${name} PRIVATE interface_mylog mylog_sanitized)

endfunction()

# 只使用核心 API 的样例不链接 Lua
gen_bin(1_a_simple_start)

# 这三个样例调用 mylog::reloadConfig()，需要 configer_lua.hpp 与 LuaJIT
function(gen_bin_lua name)

    add_executable(${name} ${name}.cpp)

    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(${name} PRIVATE mylog_lua mylog_sanitized)

endfunction()

gen_bin_lua(2_set_suppress_level)

gen_bin_lua(3_output_to_file)

gen_bin_lua(4_output_to_file_per_thread)
