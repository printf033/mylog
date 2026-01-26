add_library(interface_mylog INTERFACE)

find_package(nlohmann_json 3.12 REQUIRED)

target_link_libraries(interface_mylog INTERFACE
    nlohmann_json::nlohmann_json
)

target_compile_features(interface_mylog INTERFACE
    cxx_std_20
)

target_compile_options(interface_mylog INTERFACE
    -Wall
    -Wextra
    -Wshadow
    "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/="
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")

    target_compile_options(interface_mylog INTERFACE
        -fsanitize=address
    )

    target_link_options(interface_mylog INTERFACE
        -fsanitize=address
    )

endif()

target_compile_definitions(interface_mylog INTERFACE
    USE_NLOHMANN_JSON
)
