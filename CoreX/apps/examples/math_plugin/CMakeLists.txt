# Math Service Plugin — 编译为独立 .so 供 CoreXDaemon 加载
cmake_minimum_required(VERSION 3.8)

# 需要与 CoreX 相同的编译选项
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Protobuf REQUIRED)

# 生成 Protobuf 文件（如果还没有生成）
# 假设 proto 目录已有生成的 .pb.h/.pb.cc

add_library(math_service SHARED
    math_service_plugin.cpp
    ../../proto/math_service.pb.cc
    ../../proto/rpc_message.pb.cc
)

target_include_directories(math_service PRIVATE
    ${CMAKE_SOURCE_DIR}/../../src/rpc
    ${CMAKE_SOURCE_DIR}/../../src/net
    ${CMAKE_SOURCE_DIR}/../../src/log
    ${CMAKE_SOURCE_DIR}/../../src/ipc
    ${CMAKE_SOURCE_DIR}/../../proto
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(math_service PRIVATE
    ${Protobuf_LIBRARIES}
)

# 输出到 plugins 目录
set_target_properties(math_service PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../../build/plugins"
)
