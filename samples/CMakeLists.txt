find_package(nlohmann_json 3.12 REQUIRED)

function(gen_bin name)

    add_executable(${name} ${name}.cpp)

    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(${name} PRIVATE nlohmann_json::nlohmann_json)

    target_compile_options(${name} PRIVATE
        "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/="
        -fsanitize=address
        -g
    )

    target_link_options(${name} PRIVATE -fsanitize=address)

endfunction()

gen_bin(1_a_simple_start)

gen_bin(2_set_suppress_level)

gen_bin(3_output_to_file)

gen_bin(4_output_to_file_with_multi_buffer)
