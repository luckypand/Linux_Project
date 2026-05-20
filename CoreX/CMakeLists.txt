
cmake_minimum_required(VERSION 3.8)
project(CoreX LANGUAGES CXX)

# Use C++17 by default
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find thread library
find_package(Threads REQUIRED)

# Log library sources
set(LOG_SOURCES
    src/log/my_log.cpp
    src/log/my_buffer.cpp
)

# Net library sources
set(NET_SOURCES
    src/net/EventLoop.cpp
    src/net/Poller.cpp
    src/net/Channel.cpp
    src/net/HeapTimer.cpp
    src/net/Buffer.cpp
    src/net/Socket.cpp
    src/net/Acceptor.cpp
    src/net/TcpServer.cpp
    src/net/TcpConnection.cpp
    src/net/EventLoopThreadPool.cpp
    src/net/EventLoopThread.cpp
    ${LOG_SOURCES}
)

# Test executables
add_executable(timeout_server_test
    tests/timeout_server_test.cpp
    ${NET_SOURCES}
)

target_include_directories(timeout_server_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/log
)

target_link_libraries(timeout_server_test PRIVATE Threads::Threads)

# Test integration
add_executable(test_net_integration
    tests/test_net_integration.cpp
    ${NET_SOURCES}
)

target_include_directories(test_net_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/log
)

target_link_libraries(test_net_integration PRIVATE Threads::Threads)

# Echo server test
add_executable(test_echo_server
    tests/Test_EchoSever.cpp
    ${NET_SOURCES}
)

target_include_directories(test_echo_server PRIVATE
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/log
)

target_link_libraries(test_echo_server PRIVATE Threads::Threads)
