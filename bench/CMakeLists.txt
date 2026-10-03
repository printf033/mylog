add_executable(bench bench.cpp)

target_include_directories(bench PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(bench PRIVATE interface_mylog)

# 基准测试**有意不链接** mylog_sanitized：ASan/UBSan 插桩会淹没真实性能差异。
# 同时强制 -O2 -DNDEBUG，因为 Debug 构建默认是 -g（无优化）。
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bench PRIVATE -O2 -DNDEBUG)
endif()
