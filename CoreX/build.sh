#!/bin/bash
set -e

# 清理旧构建
rm -rf build

# 创建并进入构建目录
mkdir build && cd build

# 配置 CMake
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 编译
make -j$(nproc)

# 运行测试（可选）
echo "Build complete! Test executable at: ./timeout_server_test"