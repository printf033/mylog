function(gen_bin name)

    add_executable(${name} ${name}.cpp)

    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(${name} PRIVATE interface_mylog)

endfunction()

gen_bin(1_a_simple_start)

gen_bin(2_set_suppress_level)

gen_bin(3_output_to_file)

gen_bin(4_output_to_file_with_multi_buffer)
