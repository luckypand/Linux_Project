
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

# Test_net — 全模块综合测试
add_executable(test_net
    tests/Test_net.cpp
    ${NET_SOURCES}
)

target_include_directories(test_net PRIVATE
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/log
)

target_link_libraries(test_net PRIVATE Threads::Threads)

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
    proto/robot_service.pb.cc
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
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/net>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/log>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/rpc>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/ipc>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/proto>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/CoreX>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/CoreX/proto>
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

# ============================================================
# CoreXDaemon — 独立 RPC 中间件守护进程
# ============================================================

# 查找 yaml-cpp（用于配置文件解析）
find_package(yaml-cpp REQUIRED)

set(DAEMON_SOURCES
    apps/daemon/main.cpp
    apps/daemon/CoreXDaemon.cpp
    apps/daemon/ConfigManager.cpp
    apps/daemon/PluginLoader.cpp
    apps/daemon/SignalHandler.cpp
)

add_executable(CoreXDaemon ${DAEMON_SOURCES})

target_include_directories(CoreXDaemon PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/daemon
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/log
    ${CMAKE_SOURCE_DIR}/src/rpc
    ${CMAKE_SOURCE_DIR}/src/ipc
    ${CMAKE_SOURCE_DIR}/proto
    ${Protobuf_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIRS}
)

target_link_libraries(CoreXDaemon PRIVATE
    -Wl,--whole-archive rpc -Wl,--no-whole-archive
    yaml-cpp
    ${CMAKE_DL_LIBS}   # dlopen, dlsym, dlclose
)

# 导出所有符号，使 dlopen 加载的 .so 插件能访问主程序的 Protobuf 符号
set_target_properties(CoreXDaemon PROPERTIES ENABLE_EXPORTS TRUE)

# ============================================================
# Math Service Plugin — 示例业务插件 .so
# 注意：不编译 proto 文件，proto 符号由主程序 librpc.a 提供
# ============================================================
add_library(math_service SHARED
    apps/examples/math_plugin/math_service_plugin.cpp
)

target_include_directories(math_service PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rpc
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/log
    ${CMAKE_SOURCE_DIR}/src/ipc
    ${CMAKE_SOURCE_DIR}/proto
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(math_service PRIVATE
    ${Protobuf_LIBRARIES}
)

# 输出到 build/plugins/ 目录
set_target_properties(math_service PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# ============================================================
# Host Controller — 机器人遥控主机（纯客户端，无需 ROS）
# ============================================================
add_executable(host_controller
    apps/examples/robot_controller/main.cpp
)

target_include_directories(host_controller PRIVATE
    ${CMAKE_SOURCE_DIR}/src/net
    ${CMAKE_SOURCE_DIR}/src/log
    ${CMAKE_SOURCE_DIR}/src/rpc
    ${CMAKE_SOURCE_DIR}/src/ipc
    ${CMAKE_SOURCE_DIR}/proto
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(host_controller PRIVATE
    rpc
)

# ============================================================
# ROS 插件 — 机器人在车端部署（需要 ROS 环境才能编译）
# 如果本机未安装 ROS，这些 target 会被跳过
# ============================================================
find_package(catkin QUIET COMPONENTS roscpp nav_msgs geometry_msgs)

if(catkin_FOUND)
    message(STATUS "ROS found — building robot plugins")

    # --- Telemetry Plugin (里程计 + 状态遥测) ---
    add_library(telemetry_plugin SHARED
        apps/plugins/ros/telemetry_plugin.cpp
    )

    target_include_directories(telemetry_plugin PRIVATE
        ${CMAKE_SOURCE_DIR}/src/rpc
        ${CMAKE_SOURCE_DIR}/src/net
        ${CMAKE_SOURCE_DIR}/src/log
        ${CMAKE_SOURCE_DIR}/src/ipc
        ${CMAKE_SOURCE_DIR}/proto
        ${Protobuf_INCLUDE_DIRS}
        ${catkin_INCLUDE_DIRS}
    )

    target_link_libraries(telemetry_plugin PRIVATE
        ${Protobuf_LIBRARIES}
        ${catkin_LIBRARIES}
    )

    set_target_properties(telemetry_plugin PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )

    # --- Control Plugin (运动控制) ---
    add_library(control_plugin SHARED
        apps/plugins/ros/control_plugin.cpp
    )

    target_include_directories(control_plugin PRIVATE
        ${CMAKE_SOURCE_DIR}/src/rpc
        ${CMAKE_SOURCE_DIR}/src/net
        ${CMAKE_SOURCE_DIR}/src/log
        ${CMAKE_SOURCE_DIR}/src/ipc
        ${CMAKE_SOURCE_DIR}/proto
        ${Protobuf_INCLUDE_DIRS}
        ${catkin_INCLUDE_DIRS}
    )

    target_link_libraries(control_plugin PRIVATE
        ${Protobuf_LIBRARIES}
        ${catkin_LIBRARIES}
    )

    set_target_properties(control_plugin PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )

else()
    message(STATUS "ROS NOT found — skipping robot plugins (build on robot instead)")
    message(STATUS "  插件源码位于 apps/plugins/ros/，部署到机器人后编译")
endif()

# ============================================================
# 安装目标
# ============================================================
include(GNUInstallDirs)

# 安装静态库和守护进程
install(TARGETS rpc CoreXDaemon
    EXPORT CoreXTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# 安装头文件
install(DIRECTORY src/net src/rpc src/ipc src/log
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CoreX
    FILES_MATCHING PATTERN "*.hpp"
)

# 安装 protobuf 生成文件
install(DIRECTORY proto/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CoreX/proto
    FILES_MATCHING PATTERN "*.pb.h"
    PATTERN "*.pb.cc" EXCLUDE
)

# 安装默认配置文件
install(FILES apps/config/corex_daemon.yaml
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/corex
)

# ============================================================
# CMake 导出配置（供 find_package(CoreX) 使用）
# ============================================================
install(EXPORT CoreXTargets
    FILE CoreXTargets.cmake
    NAMESPACE CoreX::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CoreX
)
