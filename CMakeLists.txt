cmake_minimum_required(VERSION 3.16)

project(mylog)

set(CMAKE_CXX_STANDARD 26)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MYLOG_BUILD_EXAMPLES "Build example programs" ON)
option(MYLOG_BUILD_TESTS "Build tests" ON)
option(MYLOG_BUILD_BENCH "Build benchmarks (建议用 Release 构建)" ON)

add_subdirectory(src)

if(MYLOG_BUILD_EXAMPLES OR MYLOG_BUILD_SAMPLES)
    add_subdirectory(examples)
endif()

if(MYLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MYLOG_BUILD_BENCH)
    add_subdirectory(bench)
endif()

find_program(CLANG_FORMAT_EXE clang-format)
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE MYLOG_CXX_FILES
        ${PROJECT_SOURCE_DIR}/src/*.hpp
        ${PROJECT_SOURCE_DIR}/src/*.cpp
        ${PROJECT_SOURCE_DIR}/examples/*.hpp
        ${PROJECT_SOURCE_DIR}/examples/*.cpp
        ${PROJECT_SOURCE_DIR}/tests/*.hpp
        ${PROJECT_SOURCE_DIR}/tests/*.cpp
        ${PROJECT_SOURCE_DIR}/bench/*.hpp
        ${PROJECT_SOURCE_DIR}/bench/*.cpp
    )

    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${MYLOG_CXX_FILES}
        COMMENT "Auto-formatting all C++ files with clang-format"
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run -Werror ${MYLOG_CXX_FILES}
        COMMENT "Checking C++ files with clang-format"
    )
endif()

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.githooks/pre-commit")
    if(UNIX)
        execute_process(
            COMMAND chmod +x "${CMAKE_CURRENT_SOURCE_DIR}/.githooks/pre-commit"
            OUTPUT_QUIET ERROR_QUIET
        )
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --is-inside-work-tree
        RESULT_VARIABLE _GIT_WORK_TREE_RES
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_GIT_WORK_TREE_RES EQUAL 0)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} config core.hooksPath .githooks
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE _GIT_HOOK_RES
            OUTPUT_QUIET ERROR_QUIET
        )
        if(_GIT_HOOK_RES EQUAL 0)
            message(STATUS "Git pre-commit hook configured: core.hooksPath -> .githooks")
        endif()
    endif()
endif()

