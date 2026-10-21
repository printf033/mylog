set(TEST_SOURCES
    main.cpp
    test_common.cpp
    test_logger.cpp
    test_file_manager.cpp
    test_config_lua.cpp
    test_reflection.cpp
)

add_executable(mylog_test ${TEST_SOURCES})

# Include test helpers directory (test_common.hpp); mylog and Lua headers are propagated by target_link_libraries
target_include_directories(mylog_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mylog_test PRIVATE
    mylog::mylog_lua
    mylog::mylog_sanitized
)

add_test(NAME mylog_test COMMAND mylog_test)

option(MYLOG_BUILD_TSAN_TEST "Build the ThreadSanitizer test variant" OFF)
if(MYLOG_BUILD_TSAN_TEST)
    add_executable(test_mylog_tsan ${TEST_SOURCES})
    target_include_directories(test_mylog_tsan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_mylog_tsan PRIVATE mylog::mylog_lua)
    target_compile_options(test_mylog_tsan PRIVATE -fsanitize=thread -g)
    target_link_options(test_mylog_tsan PRIVATE -fsanitize=thread)
endif()
