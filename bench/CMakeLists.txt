add_executable(bench bench.cpp)

target_include_directories(bench PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(bench PRIVATE mylog)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bench PRIVATE -O2 -DNDEBUG)
endif()
