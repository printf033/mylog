add_library(interface_mylog INTERFACE)

find_package(Lua 5.4 REQUIRED)

target_link_libraries(interface_mylog INTERFACE
    ${LUA_LIBRARIES}
)

target_include_directories(interface_mylog INTERFACE
    ${CMAKE_SOURCE_DIR}/src
    ${LUA_INCLUDE_DIRS}
)

target_compile_features(interface_mylog INTERFACE
    cxx_std_20
)

target_compile_options(interface_mylog INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=>
    $<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-fsanitize=address -fno-omit-frame-pointer>
)

target_link_options(interface_mylog INTERFACE
    $<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-fsanitize=address>
)

target_compile_definitions(interface_mylog INTERFACE
    # USE_LUA
)
