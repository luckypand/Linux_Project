#!/bin/bash
# ============================================================
# RPC Benchmark Flame Graph Generator
# ============================================================
# 用法:
#   sudo ./tests/perf_flamegraph_rpc.sh [port] [threads] [calls]
#
# 示例:
#   sudo ./tests/perf_flamegraph_rpc.sh                        # 默认参数
#   sudo ./tests/perf_flamegraph_rpc.sh 8080 4 200000          # 自定义
# ============================================================
set -e

PORT=${1:-8080}
THREADS=${2:-2}
CALLS=${3:-100000}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COREX_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$COREX_DIR/build"
FLAMEGRAPH_DIR="/root/Cplus/FlameGraph"
PERF_DATA="/tmp/perf_rpc_$(date +%Y%m%d_%H%M%S).data"
OUTPUT_SVG="$COREX_DIR/rpc_benchmark_flamegraph.svg"

echo "============================================"
echo "  RPC Benchmark Flame Graph Generator"
echo "============================================"
echo "  Port:       $PORT"
echo "  Threads:    $THREADS"
echo "  Calls:      $CALLS"
echo "  Perf data:  $PERF_DATA"
echo "  Output SVG: $OUTPUT_SVG"
echo "============================================"
echo ""

# ---- 1. 构建 ----
echo "[1/5] 构建 test_rpc_benchmark (Release)..."
cd "$COREX_DIR"
./build.sh release test_rpc_benchmark
echo ""

# ---- 2. 启动服务器（perf 采样） ----
echo "[2/5] 启动 RPC 服务器 (perf record)..."
sudo perf record -F 99 -g --call-graph dwarf -o "$PERF_DATA" \
    "$BUILD_DIR/test_rpc_benchmark" --mode server --port "$PORT" &
PERF_PID=$!

# 等待服务器就绪
sleep 2

# 检查服务器是否仍在运行
if ! kill -0 $PERF_PID 2>/dev/null; then
    echo "[错误] 服务器启动失败"
    exit 1
fi
echo "  服务器 PID: $(pgrep -f 'test_rpc_benchmark.*server' | head -1)"
echo ""

# ---- 3. 运行客户端压测 ----
echo "[3/5] 运行客户端压测 ($CALLS 次调用, $THREADS 线程)..."
"$BUILD_DIR/test_rpc_benchmark" \
    --mode benchmark \
    --port "$PORT" \
    --threads "$THREADS" \
    --calls "$CALLS"
CLIENT_EXIT=$?
echo ""

# ---- 4. 停止服务器 ----
echo "[4/5] 停止服务器并等待 perf 刷新数据..."
# 找到 RPC 服务器进程并发送 SIGINT
SERVER_PID=$(pgrep -f 'test_rpc_benchmark.*--mode server' 2>/dev/null || true)
if [ -n "$SERVER_PID" ]; then
    sudo kill -INT "$SERVER_PID" 2>/dev/null || true
fi

# 等待 perf 进程退出
wait $PERF_PID 2>/dev/null || true

# 确认 perf 数据已生成
if [ ! -f "$PERF_DATA" ]; then
    echo "[错误] perf 数据文件未生成: $PERF_DATA"
    exit 1
fi
echo "  perf 数据大小: $(du -h "$PERF_DATA" | cut -f1)"
echo ""

# ---- 5. 生成火焰图 SVG ----
echo "[5/5] 生成火焰图 SVG..."
if [ ! -f "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" ]; then
    echo "[警告] stackcollapse-perf.pl 未找到于 $FLAMEGRAPH_DIR"
    echo "[警告] 跳过火焰图生成，perf 数据保存于: $PERF_DATA"
    echo "[警告] 手动处理: perf script -i $PERF_DATA | stackcollapse-perf.pl | flamegraph.pl > out.svg"
    exit 0
fi

sudo perf script -i "$PERF_DATA" 2>/dev/null | \
    "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" | \
    "$FLAMEGRAPH_DIR/flamegraph.pl" \
        --title "CoreX RPC Benchmark - CPU Hotspots ($CALLS calls, $THREADS threads)" \
        --width 1200 \
        --height 16 \
        --colors java \
        --minwidth 0.5 \
    > "$OUTPUT_SVG"

# 清理临时 perf 数据
rm -f "$PERF_DATA"

echo ""
echo "============================================"
echo "  火焰图已生成!"
echo "  $OUTPUT_SVG"
echo "  (使用浏览器打开 SVG 文件查看)"
echo "============================================"

# 返回客户端压测的退出码
exit $CLIENT_EXIT
