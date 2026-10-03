cmake_minimum_required(VERSION 3.16)

project(mylog)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(MYLOG_BUILD_SAMPLES "Build sample programs" ON)
option(MYLOG_BUILD_TESTS "Build tests" ON)
option(MYLOG_BUILD_BENCH "Build benchmarks (建议用 Release 构建)" ON)

add_subdirectory(src)

if(MYLOG_BUILD_SAMPLES)
    add_subdirectory(sample)
endif()

if(MYLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(MYLOG_BUILD_BENCH)
    add_subdirectory(bench)
endif()
