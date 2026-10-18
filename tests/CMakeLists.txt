add_executable(mylog_test test.cpp)

target_include_directories(mylog_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(mylog_test PRIVATE mylog_lua mylog_sanitized)

add_test(NAME mylog_test COMMAND mylog_test)

option(MYLOG_BUILD_TSAN_TEST "Build the ThreadSanitizer test variant" OFF)
if(MYLOG_BUILD_TSAN_TEST)
    add_executable(test_mylog_tsan test.cpp)
    target_include_directories(test_mylog_tsan PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_mylog_tsan PRIVATE mylog_lua)
    target_compile_options(test_mylog_tsan PRIVATE -fsanitize=thread -g)
    target_link_options(test_mylog_tsan PRIVATE -fsanitize=thread)
endif()
