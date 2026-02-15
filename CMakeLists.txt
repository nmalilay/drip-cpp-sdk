cmake_minimum_required(VERSION 3.0)

project(drip-sdk-cpp03
    VERSION 0.1.0
    LANGUAGES CXX
)

# C++03 — the whole point of this SDK variant
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(DRIP_BUILD_EXAMPLES "Build example programs" OFF)
option(DRIP_BUILD_TESTS "Build tests" OFF)
option(DRIP_VENDORED_JSON "Use vendored nlohmann/json instead of FetchContent" OFF)

# =============================================================================
# Dependencies
# =============================================================================

# libcurl - required for HTTP
find_package(CURL REQUIRED)

# nlohmann/json - header-only JSON library
if(DRIP_VENDORED_JSON)
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    )
else()
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# =============================================================================
# Library
# =============================================================================

add_library(drip_sdk03
    src/client.cpp
)

add_library(drip::sdk03 ALIAS drip_sdk03)

target_include_directories(drip_sdk03
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(drip_sdk03
    PRIVATE
        nlohmann_json::nlohmann_json
        CURL::libcurl
)

set_target_properties(drip_sdk03 PROPERTIES
    OUTPUT_NAME "drip03"
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

# =============================================================================
# Examples
# =============================================================================

if(DRIP_BUILD_EXAMPLES)
    add_executable(drip03_demo examples/demo.cpp)
    target_link_libraries(drip03_demo PRIVATE drip_sdk03)
endif()

# =============================================================================
# Tests
# =============================================================================

if(DRIP_BUILD_TESTS)
    enable_testing()
    add_executable(drip03_tests tests/test_client.cpp)
    target_link_libraries(drip03_tests PRIVATE drip_sdk03)
    add_test(NAME drip03_sdk_tests COMMAND drip03_tests)
endif()

# =============================================================================
# Install
# =============================================================================

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(GNUInstallDirs)

    install(TARGETS drip_sdk03
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(DIRECTORY include/drip
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
