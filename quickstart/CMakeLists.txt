cmake_minimum_required(VERSION 3.14)
project(drip-quickstart LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch the Drip C++ SDK from GitHub
include(FetchContent)
FetchContent_Declare(
    drip_sdk
    GIT_REPOSITORY https://github.com/nmalilay/drip-cpp-sdk.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(drip_sdk)

add_executable(drip_test main.cpp)
target_link_libraries(drip_test PRIVATE drip::sdk)
