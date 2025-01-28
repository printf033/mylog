cmake_minimum_required(VERSION 3.0)

project(MYLOG)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build)

include_directories(${PROJECT_SOURCE_DIR}/inc)

aux_source_directory(${PROJECT_SOURCE_DIR}/src LOG_SRC)

add_executable(how_to_use.out ${PROJECT_SOURCE_DIR}/sample/0_how_to_use.cpp ${LOG_SRC})
add_executable(how_to_set_level.out ${PROJECT_SOURCE_DIR}/sample/1_how_to_set_level.cpp ${LOG_SRC})
add_executable(how_to_change_color.out ${PROJECT_SOURCE_DIR}/sample/2_how_to_change_color.cpp ${LOG_SRC})
add_executable(how_to_output_file.out ${PROJECT_SOURCE_DIR}/sample/3_how_to_output_file.cpp ${LOG_SRC})
add_executable(how_to_output_file_in_multithread.out ${PROJECT_SOURCE_DIR}/sample/4_how_to_output_file_in_multithread.cpp ${LOG_SRC})