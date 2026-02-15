cmake_minimum_required(VERSION 3.14)

project(drip-sdk-cpp
    VERSION 0.1.0
    LANGUAGES CXX
)

# C++03 — the whole point of this SDK variant
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(DRIP_BUILD_EXAMPLES "Build example programs" OFF)
option(DRIP_BUILD_TESTS "Build tests" OFF)

# =============================================================================
# Dependencies
# =============================================================================

# libcurl - required for HTTP
find_package(CURL REQUIRED)

# picojson - vendored header-only JSON library (C++03 compatible)
add_library(picojson INTERFACE)
target_include_directories(picojson INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# =============================================================================
# Library
# =============================================================================

add_library(drip_sdk
    src/client.cpp
)

add_library(drip::sdk ALIAS drip_sdk)

target_include_directories(drip_sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(drip_sdk
    PRIVATE
        picojson
        CURL::libcurl
)

set_target_properties(drip_sdk PROPERTIES
    OUTPUT_NAME "drip"
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

# =============================================================================
# Examples
# =============================================================================

if(DRIP_BUILD_EXAMPLES)
    add_executable(drip_demo examples/demo.cpp)
    target_link_libraries(drip_demo PRIVATE drip_sdk)
endif()

# =============================================================================
# Tests
# =============================================================================

if(DRIP_BUILD_TESTS)
    enable_testing()
    add_executable(drip_tests tests/test_client.cpp)
    target_link_libraries(drip_tests PRIVATE drip_sdk)
    add_test(NAME drip_sdk_tests COMMAND drip_tests)
endif()

# =============================================================================
# Install
# =============================================================================

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(GNUInstallDirs)

    install(TARGETS drip_sdk
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(DIRECTORY include/drip
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
