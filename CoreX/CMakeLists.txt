
cmake_minimum_required(VERSION 3.8)
project(CoreX LANGUAGES CXX)

# Use C++17 by default
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Net library sources
set(NET_SOURCES
    src/net/EventLoop.cpp
    src/net/Poller.cpp
    src/net/Channel.cpp
    src/net/HeapTimer.cpp
    src/net/Buffer.cpp
)

# Test executables
add_executable(timeout_server_test
    tests/timeout_server_test.cpp
    ${NET_SOURCES}
)

target_include_directories(timeout_server_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/net
)
