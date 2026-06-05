#!/bin/bash
# ============================================================
# CoreX 构建脚本
# ============================================================
# 用法:
#   ./build.sh                          # 默认: Release 构建所有目标
#   ./build.sh debug                    # Debug 构建所有目标
#   ./build.sh asan                     # Debug + ASan 构建所有目标
#   ./build.sh release                  # Release 构建所有目标
#   ./build.sh clean                    # 仅清理 build 目录
#   ./build.sh <target>                 # Release 构建指定目标
#   ./build.sh debug <target>           # Debug 构建指定目标
#   ./build.sh asan <target>            # Debug + ASan 构建指定目标
#
# 可用目标 (target):
#   test_echo_server        Echo 服务器/客户端压测程序
#   timeout_server_test     超时服务器测试
#   test_net_integration    网络库集成测试
#   (不指定则构建全部)
#
# 示例:
#   ./build.sh asan test_echo_server    # ASan 模式只编译 echo 测试
#   ./build.sh debug                    # Debug 模式编译全部
#   ./build.sh clean                    # 清理构建产物
# ============================================================
set -e

BUILD_TYPE="Release"
ENABLE_ASAN="OFF"
TARGET=""
DO_CLEAN_ONLY=false

# ---------- 解析参数 ----------
for arg in "$@"; do
    case "$arg" in
        debug)
            BUILD_TYPE="Debug"
            ;;
        release)
            BUILD_TYPE="Release"
            ;;
        asan)
            BUILD_TYPE="Debug"
            ENABLE_ASAN="ON"
            ;;
        clean)
            DO_CLEAN_ONLY=true
            ;;
        *)
            TARGET="$arg"
            ;;
    esac
done

# ---------- 清理 ----------
if $DO_CLEAN_ONLY; then
    echo "[build] Cleaning build directory..."
    rm -rf build
    echo "[build] Clean done."
    exit 0
fi

# ---------- CMake 配置 ----------
echo "[build] BUILD_TYPE = $BUILD_TYPE"
echo "[build] ENABLE_ASAN = $ENABLE_ASAN"
echo "[build] TARGET     = ${TARGET:-<all>}"

rm -rf build
mkdir build && cd build

cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DENABLE_ASAN="$ENABLE_ASAN" ..

# ---------- 编译 ----------
if [ -n "$TARGET" ]; then
    make -j$(nproc) "$TARGET"
else
    make -j$(nproc)
fi

# ---------- 结果 ----------
echo ""
echo "============================================"
echo "  Build complete!"
echo "  BUILD_TYPE = $BUILD_TYPE"
echo "  ENABLE_ASAN = $ENABLE_ASAN"
echo "  Output dir = $(pwd)"
echo "============================================"
ls -lh --color=auto test_*_* 2>/dev/null || true