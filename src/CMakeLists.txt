add_library(mylog STATIC mylog.cpp)

target_include_directories(mylog PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(mylog PRIVATE -Wall -Wextra -Wpedantic)