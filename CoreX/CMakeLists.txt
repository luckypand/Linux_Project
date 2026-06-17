
cmake_minimum_required(VERSION 3.8)
project(CoreX LANGUAGES CXX)

# Export compile_commands.json for editors/IntelliSense.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use C++17 by default
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Feature suffix (passed from build.sh, reflects log/timestamp flags)
# ============================================================
set(FEATURE_SUFFIX "" CACHE STRING "Suffix appended to executable names based on feature flags (-log, -timestamp, etc.)")

# ============================================================
# AddressSanitizer + LeakSanitizer (ASan + LSan)
# Enable with: cmake -DENABLE_ASAN=ON ..
# ============================================================
option(ENABLE_ASAN "Enable AddressSanitizer and LeakSanitizer" OFF)

if(ENABLE_ASAN)
    message(STATUS "ASan + LSan enabled")
    set(ASAN_FLAGS "-fsanitize=address -fno-omit-frame-pointer -g -O1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ASAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_EXECUTABLE_SUFFIX "${FEATURE_SUFFIX}_ASAN")
else()
    set(CMAKE_EXECUTABLE_SUFFIX "${FEATURE_SUFFIX}")
endif()

# ============================================================
# Compile-time feature toggles
# Enable with: cmake -DENABLE_TIMESTAMP=OFF -DENABLE_LOG=OFF ..
# ============================================================
option(ENABLE_TIMESTAMP "Enable RPC latency timing instrumentation" OFF)
option(ENABLE_LOG "Enable log printing" OFF)

message(STATUS "ENABLE_TIMESTAMP = ${ENABLE_TIMESTAMP}")
message(STATUS "ENABLE_LOG = ${ENABLE_LOG}")

add_compile_definitions(
    ENABLE_TIMESTAMP=$<BOOL:${ENABLE_TIMESTAMP}>
    ENABLE_LOG=$<BOOL:${ENABLE_LOG}>
)

# Find thread library
find_package(Threads REQUIRED)

# Find Protobuf
find_package(Protobuf REQUIRED)

# Find OpenSSL (for MD5 in IPC tests)
find_package(OpenSSL REQUIRED)

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

# ============================================================
# IPC library sources (shared-memory ring buffer)
# ============================================================
set(IPC_SOURCES
    src/ipc/ShmSegment.cpp
    src/ipc/RingBuffer.cpp
    src/ipc/IpcEndpoint.cpp
)

# ============================================================
# Proto sources (pre-generated .pb.cc files)
# ============================================================
set(PROTO_SOURCES
    proto/rpc_message.pb.cc
    proto/math_service.pb.cc
)

# ============================================================
# RPC library
# ============================================================
add_library(rpc STATIC
    src/rpc/RpcCodec.cpp
    src/rpc/RpcServer.cpp
    src/rpc/IpcRpcBridge.cpp
    ${NET_SOURCES}
    ${IPC_SOURCES}
    ${PROTO_SOURCES}
)

target_include_directories(rpc PUBLIC
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/log
    ${CMAKE_SOURCE_DIR}/src/rpc
    ${CMAKE_SOURCE_DIR}/src/ipc
    ${CMAKE_SOURCE_DIR}/proto
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(rpc PUBLIC
    Threads::Threads
    ${Protobuf_LIBRARIES}
    rt
)

# ============================================================
# RPC Benchmark Test
# ============================================================
add_executable(test_rpc_benchmark
    tests/test_rpc_benchmark.cpp
)

target_include_directories(test_rpc_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/log
    ${CMAKE_SOURCE_DIR}/src/rpc
    ${CMAKE_SOURCE_DIR}/proto
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(test_rpc_benchmark PRIVATE
    rpc
)

# ============================================================
# IPC Lock-Free Queue Benchmark Test (test_ipc)
# ============================================================
add_executable(test_ipc
    tests/test_ipc.cpp
    ${IPC_SOURCES}
)

target_include_directories(test_ipc PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ipc
    ${CMAKE_SOURCE_DIR}/src/log
)

target_link_libraries(test_ipc PRIVATE
    Threads::Threads
    OpenSSL::Crypto
    rt
)
