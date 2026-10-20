function(gen_bin name)

    add_executable(${name} ${name}.cpp)

    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)

    target_link_libraries(${name} PRIVATE mylog mylog_sanitized)

endfunction()

gen_bin(1_a_simple_start)
gen_bin(5_reflection)

function(gen_bin_lua name)

    add_executable(${name} ${name}.cpp)

    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)

    target_link_libraries(${name} PRIVATE mylog_lua mylog_sanitized)

endfunction()

gen_bin_lua(2_set_suppress_level)

gen_bin_lua(3_output_to_file)

gen_bin_lua(4_output_to_file_per_thread)
