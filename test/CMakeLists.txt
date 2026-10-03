add_executable(test_mylog test_mylog.cpp)

target_include_directories(test_mylog PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test_mylog PRIVATE mylog_lua mylog_sanitized)

add_test(NAME test_mylog COMMAND test_mylog)

# 线程竞态单独跑一遍 TSan（与 ASan 不兼容，所以用独立目标）。
# 有意不链接 mylog_sanitized：TSan 与 ASan 不能同时插桩。
option(MYLOG_BUILD_TSAN_TEST "Build the ThreadSanitizer test variant" OFF)
if(MYLOG_BUILD_TSAN_TEST)
    add_executable(test_mylog_tsan test_mylog.cpp)
    target_include_directories(test_mylog_tsan PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_mylog_tsan PRIVATE mylog_lua)
    target_compile_options(test_mylog_tsan PRIVATE -fsanitize=thread -g)
    target_link_options(test_mylog_tsan PRIVATE -fsanitize=thread)
endif()
