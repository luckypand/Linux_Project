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
#
# 说明:
#   直接在 benchmark 模式上运行 perf record，采样整个压测过程
#   （内嵌服务器 + 多线程客户端在同一进程内）。
# ============================================================
set -e

PORT=${1:-8080}
THREADS=${2:-2}
CALLS=${3:-100000}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COREX_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$COREX_DIR/build"
FLAMEGRAPH_DIR="/root/Cplus/FlameGraph"
# 时间戳: 格式 YY-MM-DD-HHMM (如 26-06-07-1415 表示 26年6月7日14:15)
TIMESTAMP=$(date +%y-%m-%d-%H%M)
PERF_DATA="$COREX_DIR/perf_data/perf_rpc_${TIMESTAMP}.data"
OUTPUT_SVG="$COREX_DIR/svg/rpc_benchmark_flamegraph_${TIMESTAMP}.svg"

echo "============================================"
echo "  RPC Benchmark Flame Graph Generator"
echo "============================================"
echo "  Port:       $PORT"
echo "  Threads:    $THREADS"
echo "  Calls:      $CALLS"
echo "  Time:       $TIMESTAMP"
echo "  Perf data:  $PERF_DATA"
echo "  Output SVG: $OUTPUT_SVG"
echo "============================================"
echo ""

# 确保输出目录存在
mkdir -p "$COREX_DIR/perf_data"
mkdir -p "$COREX_DIR/svg"

# ---- 1. 构建 ----
echo "[1/4] 构建 test_rpc_benchmark (Release)..."
cd "$COREX_DIR"
./build.sh release test_rpc_benchmark

# 确认二进制存在且可执行
ls -lh "$BUILD_DIR/test_rpc_benchmark"
echo ""

# ---- 2. 运行 benchmark + perf 采样 ----
echo "[2/4] 启动 benchmark 模式 (perf record 采样)..."
echo "  (内嵌服务器 + 客户端线程在同一进程中，perf 捕获全部 CPU 热点)"
# perf record 返回被追踪命令的退出码，benchmark 可能 FAIL 返回非零
# 临时关闭 set -e，确保即使 benchmark 失败也能继续生成火焰图
set +e
sudo perf record -F 99 -g --call-graph dwarf -o "$PERF_DATA" \
    "$BUILD_DIR/test_rpc_benchmark" \
    --mode benchmark \
    --port "$PORT" \
    --threads "$THREADS" \
    --calls "$CALLS"
BENCH_EXIT=$?
set -e

echo ""
echo "  benchmark 退出码: $BENCH_EXIT"

# ---- 3. 确认 perf 数据 ----
echo ""
echo "[3/4] 确认 perf 采样数据..."
if [ ! -f "$PERF_DATA" ]; then
    echo "[错误] perf 数据文件未生成: $PERF_DATA"
    exit 1
fi
echo "  perf 数据大小: $(du -h "$PERF_DATA" | cut -f1)"
echo ""

# ---- 4. 生成火焰图 SVG ----
echo "[4/4] 生成火焰图 SVG..."
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

echo ""
echo "============================================"
echo "  火焰图已生成!"
echo "  $OUTPUT_SVG"
echo "  (使用浏览器打开 SVG 文件查看)"
echo "============================================"

# 返回 benchmark 的退出码
exit $BENCH_EXIT
