cmake_minimum_required(VERSION 3.20)

project(mylog
    VERSION 1.0.0
    DESCRIPTION "A modern, high-performance, header-only C++26 logging library"
    LANGUAGES CXX
)

# -----------------------------------------------------------------------------
# Detect if mylog is built as a top-level project or included as a subproject
# -----------------------------------------------------------------------------
set(MYLOG_IS_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(MYLOG_IS_TOP_LEVEL ON)
endif()

# Global C++ standard settings (primarily for top-level targets)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Top-level defaults
if(MYLOG_IS_TOP_LEVEL)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# -----------------------------------------------------------------------------
# Options: default to ON for standalone builds, OFF when consumed as a subproject
# -----------------------------------------------------------------------------
option(MYLOG_BUILD_EXAMPLES "Build example programs" ${MYLOG_IS_TOP_LEVEL})
option(MYLOG_BUILD_TESTS    "Build tests"            ${MYLOG_IS_TOP_LEVEL})
option(MYLOG_BUILD_BENCH    "Build benchmarks"       ${MYLOG_IS_TOP_LEVEL})

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------
add_subdirectory(src)

if(MYLOG_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(MYLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MYLOG_BUILD_BENCH)
    add_subdirectory(bench)
endif()

# -----------------------------------------------------------------------------
# Developer Tooling (only active when building as top-level project)
# -----------------------------------------------------------------------------
if(MYLOG_IS_TOP_LEVEL)
    find_program(CLANG_FORMAT_EXE clang-format)
    if(CLANG_FORMAT_EXE)
        file(GLOB_RECURSE MYLOG_CXX_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/bench/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/bench/*.cpp"
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
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                RESULT_VARIABLE _GIT_HOOK_RES
                OUTPUT_QUIET ERROR_QUIET
            )
            if(_GIT_HOOK_RES EQUAL 0)
                message(STATUS "Git pre-commit hook configured: core.hooksPath -> .githooks")
            endif()
        endif()
    endif()
endif()
