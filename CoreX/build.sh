#!/bin/bash
# ============================================================
# CoreX 构建脚本
# ============================================================
# 用法:
#   ./build.sh [选项...] [target]
#
# 构建模式:
#   （无）                              默认: Release 构建
#   debug                               Debug 构建
#   asan                                Debug + AddressSanitizer 构建
#   release                             Release 构建（显式）
#   clean                               仅清理 build 目录
#
# 功能开关（默认均为 OFF，需显式开启）:
#   timestamp=1                         开启 RPC 耗时测量（now_us / t0-t4 计时代码）
#   timestamp=0                         关闭 RPC 耗时测量（默认）
#   log=1                               开启日志打印（LOG_* 宏正常展开）
#   log=0                               关闭日志打印（默认，LOG_* 展开为空操作）
#
# 可用目标 (target):
#   test_echo_server        Echo 服务器/客户端压测程序
#   timeout_server_test     超时服务器测试
#   test_net_integration    网络库集成测试
#   test_rpc_benchmark      RPC 基准测试
#   （不指定则构建全部）
#
# 示例:
#   ./build.sh                                          # 默认构建，两个功能均关闭
#   ./build.sh timestamp=1 log=1                        # 全部功能开启
#   ./build.sh log=1 test_rpc_benchmark                 # 仅开日志，构建 RPC 基准测试
#   ./build.sh timestamp=1 log=1 asan test_echo_server  # ASan + 全功能，只编译 echo
#   ./build.sh clean                                    # 清理构建产物
# ============================================================
set -e

BUILD_TYPE="Release"
ENABLE_ASAN="OFF"
ENABLE_TIMESTAMP="OFF"
ENABLE_LOG="OFF"
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
        timestamp=0)
            ENABLE_TIMESTAMP="OFF"
            ;;
        timestamp=1)
            ENABLE_TIMESTAMP="ON"
            ;;
        log=0)
            ENABLE_LOG="OFF"
            ;;
        log=1)
            ENABLE_LOG="ON"
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

# ---------- 计算功能后缀（用于可执行文件名）----------
FEATURE_SUFFIX=""
if [ "$ENABLE_LOG" = "ON" ]; then
    FEATURE_SUFFIX="${FEATURE_SUFFIX}-log"
fi
if [ "$ENABLE_TIMESTAMP" = "ON" ]; then
    FEATURE_SUFFIX="${FEATURE_SUFFIX}-timestamp"
fi

# ---------- CMake 配置 ----------
echo "[build] BUILD_TYPE     = $BUILD_TYPE"
echo "[build] ENABLE_ASAN     = $ENABLE_ASAN"
echo "[build] TIMESTAMP       = $ENABLE_TIMESTAMP"
echo "[build] LOG             = $ENABLE_LOG"
echo "[build] FEATURE_SUFFIX  = ${FEATURE_SUFFIX:-<none>}"
echo "[build] TARGET          = ${TARGET:-<all>}"

rm -rf build
mkdir build && cd build

cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DENABLE_ASAN="$ENABLE_ASAN" \
        -DENABLE_TIMESTAMP="$ENABLE_TIMESTAMP" -DENABLE_LOG="$ENABLE_LOG" \
        -DFEATURE_SUFFIX="$FEATURE_SUFFIX" ..

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
echo "  BUILD_TYPE     = $BUILD_TYPE"
echo "  ENABLE_ASAN     = $ENABLE_ASAN"
echo "  TIMESTAMP       = $ENABLE_TIMESTAMP"
echo "  LOG             = $ENABLE_LOG"
echo "  FEATURE_SUFFIX  = ${FEATURE_SUFFIX:-<none>}"
echo "  Output dir      = $(pwd)"
echo "============================================"
ls -lh --color=auto test_* timeout_* 2>/dev/null || true