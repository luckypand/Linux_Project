#!/bin/bash
# ============================================================
# Test_net.sh — Test_net 测试文件使用说明与快捷脚本
#
# Test_net 是 CoreX 网络库 (src/net/) 的全模块综合测试程序，
# 覆盖 11 个模块共 57 个测试点，支持多种运行模式。
#
# 用法:
#   ./Test_net.sh [command] [options]
#
# ============================================================
# 支持的命令 (command)
# ============================================================
#
#   build       — 编译 Test_net (底层调用 ../build.sh test_net)
#
#   unit        — [默认] 运行全部 11 个模块的单元测试 (57 项)
#                 可通过 --test <name> 指定只测某个模块
#                 可用的 --test 名称:
#                   all buffer socket channel poller eventloop
#                   eventloopthread pool timer acceptor tcpconn tcpserver
#                 示例:
#                   ./Test_net.sh unit
#                   ./Test_net.sh unit --test buffer
#                   ./Test_net.sh unit --test timer
#
#   server      — 启动 Echo 服务器 (基于 TcpServer + EventLoopThreadPool)
#                 支持参数: --port --threads
#                 示例:
#                   ./Test_net.sh server --port 9090 --threads 4
#
#   client      — 启动压力客户端 (epoll + 非阻塞 connect + 心跳)
#                 支持参数: --server-ip --server-port --connections --duration --heartbeat
#                 示例:
#                   ./Test_net.sh client --connections 5000 --duration 30
#
#   timer       — HeapTimer 专项长时间测试
#                 使用 EventLoop + HeapTimer::tick 周期性触发
#                 支持参数: --duration
#                 示例:
#                   ./Test_net.sh timer --duration 30
#
#   eventloop   — EventLoop / EventLoopThread / EventLoopThreadPool 专项
#                 持续向多个子线程投递跨线程任务
#                 支持参数: --threads --duration
#                 示例:
#                   ./Test_net.sh eventloop --threads 4 --duration 10
#
#   benchmark   — 全栈集成基准测试 (内嵌 server + client)
#                 同时启动 Echo 服务器和压力客户端，验证端到端链路
#                 支持参数: --connections --duration --threads
#                 示例:
#                   ./Test_net.sh benchmark --connections 1000 --duration 10 --threads 4
#
#   list        — 列出所有可用的测试模块名称及简要说明
#   modules     — 以表格显示 11 个模块的测试项数量与覆盖内容
#   help        — 显示完整帮助信息
#
# ============================================================
# 测试覆盖的 11 个模块 (src/net/)
# ============================================================
#
#   模块              测试项  覆盖内容
#   ────────────────  ──────  ──────────────────────────────────
#   Buffer             14     append/peek/retrieve/prepend/clear
#                             扩容搬移/readFd/writeFd/peekInt32
#                             beginWrite+hasWritten/swap
#   Socket              4     创建+选项验证(4种setsockopt)
#                             移动语义/bind+listen/accept
#   Channel             4     读回调/写回调/enable+disable
#                             对端关闭检测(read=0)
#   Poller              3     状态机 kNEW→KADDED→KDELETED
#                             poll→Event_handle 分发
#   EventLoop           4     线程归属/RunInloop/跨线程quit
#                             WakeUp 多任务唤醒
#   EventLoopThread     2     启动+停止/多线程独立 Loop
#   EventLoopThreadPool 3     单线程回退(numThreads=0)
#                             Round-Robin 均匀性/独立子线程
#   HeapTimer           9     add+tick/adjust/多定时器/doWork
#                             clear/Getclosetick 最近超时
#   Acceptor            1     烟雾测试(构造+listen不崩溃)
#   TcpConnection       4     状态机/send+receive/send echo
#                             connectionCallback
#   TcpServer           2     start/stop 生命周期
#                             完整 Echo 集成(connect→echo→验证)
#
#   总计: 11 个模块, 57 项测试
#
# ============================================================
# 通用参数 (透传给 Test_net)
# ============================================================
#
#   --port PORT          服务器监听端口 (默认: 8080)
#   --threads N          子 Reactor 线程数 (默认: 4)
#   --connections N      客户端连接数 (默认: 1000)
#   --duration S         持续时间秒数 (默认: 10)
#   --server-ip IP       服务器 IP 地址 (默认: 127.0.0.1)
#   --server-port PORT   服务器端口 (默认: 8080)
#   --heartbeat S        心跳间隔秒数 (默认: 1)
#   --test NAME          指定测试模块 (仅 unit 模式)
#
# ============================================================
# 典型使用场景
# ============================================================
#
#   场景 1 — 开发自测 (快速验证):
#     ./Test_net.sh build && ./Test_net.sh
#
#   场景 2 — 单模块调试:
#     ./Test_net.sh unit --test buffer
#
#   场景 3 — 内存泄漏检测:
#     cd .. && ./build.sh asan test_net
#     ASAN_OPTIONS=detect_leaks=1 ../build/test_net_ASAN --mode unit
#
#   场景 4 — 并发压力测试:
#     # 终端 1: 启动服务器
#     ./Test_net.sh server --port 8080 --threads 4
#     # 终端 2: 启动客户端
#     ./Test_net.sh client --connections 10000 --duration 600
#
#   场景 5 — 全栈基准:
#     ./Test_net.sh benchmark --connections 1000 --duration 10 --threads 4
#
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
BINARY="$BUILD_DIR/test_net"

# ============================================================
# 颜色输出
# ============================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()   { echo -e "${RED}[ERR]${NC} $*"; }

# ============================================================
# 构建
# ============================================================
build() {
    info "正在编译 Test_net ..."
    cd "$PROJECT_DIR" && ./build.sh test_net
    ok "编译完成: $BINARY"
}

# ============================================================
# Module 说明
# ============================================================
show_modules() {
    echo ""
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║     Test_net — 测试模块总览 (src/net/ 全部 11 个模块)  ║"
    echo "╠══════════════════════════════════════════════════════════╣"
    echo "║                                                        ║"
    echo "║  模块              测试项  说明                        ║"
    echo "║  ────────────────  ──────  ──────────────────────────  ║"
    echo "║  buffer             14     读写/扩容/prepend/peekInt32  ║"
    echo "║                            readFd/writeFd/swap          ║"
    echo "║  socket              4     创建+选项/移动语义/accept    ║"
    echo "║  channel             4     读/写回调/enable+disable     ║"
    echo "║                            对端关闭检测 (read=0)        ║"
    echo "║  poller              3     状态机(kNEW/KADDED/KDELETED) ║"
    echo "║                            poll → Event_handle 分发     ║"
    echo "║  eventloop           4     线程归属/RunInloop/跨线程quit║"
    echo "║                            WakeUp 多任务唤醒            ║"
    echo "║  eventloopthread     2     启动+停止/多线程独立 Loop    ║"
    echo "║  pool                3     单线程回退/Round-Robin/独立  ║"
    echo "║  timer               9     add+tick/adjust/多定时器     ║"
    echo "║                            doWork/clear/Getclosetick    ║"
    echo "║  acceptor            1     烟雾测试 (构造+listen)       ║"
    echo "║  tcpconn             4     状态机/send+receive/回调     ║"
    echo "║  tcpserver           2     start+stop/完整 Echo 集成    ║"
    echo "║                                                        ║"
    echo "╠══════════════════════════════════════════════════════════╣"
    echo "║  总计: 11 个模块, 57 项测试                             ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo ""
}

# ============================================================
# 帮助
# ============================================================
show_help() {
    echo ""
    echo "Test_net.sh — Test_net 测试快捷脚本"
    echo ""
    echo "用法: $0 [command] [options]"
    echo ""
    echo "Commands:"
    echo "  build          编译 Test_net (调用 build.sh)"
    echo "  unit           运行全部单元测试 (默认)"
    echo "  server         启动 Echo 服务器"
    echo "  client         启动压力客户端"
    echo "  timer          HeapTimer 专项长时间测试"
    echo "  eventloop      EventLoop/Thread/ThreadPool 专项测试"
    echo "  benchmark      全栈集成基准测试 (内嵌 server + client)"
    echo "  list           列出所有可用测试模块"
    echo "  modules        显示测试模块总览"
    echo "  help           显示此帮助"
    echo ""
    echo "通用参数 (透传给 Test_net):"
    echo "  --port PORT          服务器端口 (默认: 8080)"
    echo "  --threads N          子 Reactor 线程数 (默认: 4)"
    echo "  --connections N      客户端连接数 (默认: 1000)"
    echo "  --duration S         持续时间秒数 (默认: 10)"
    echo "  --server-ip IP       服务器 IP (默认: 127.0.0.1)"
    echo "  --server-port PORT   服务器端口 (默认: 8080)"
    echo "  --test NAME          指定测试模块 (unit 模式)"
    echo ""
    echo "可用的 --test 名称:"
    echo "  all, buffer, socket, channel, poller, eventloop,"
    echo "  eventloopthread, pool, timer, acceptor, tcpconn, tcpserver"
    echo ""
    echo "示例:"
    echo "  $0 build                                                # 编译"
    echo "  $0                                                      # 运行全部单元测试"
    echo "  $0 unit --test buffer                                   # 只测试 Buffer"
    echo "  $0 server --port 9090 --threads 4                       # 启动 Echo 服务器"
    echo "  $0 client --connections 5000 --duration 30              # 5000 连接压力测试"
    echo "  $0 benchmark --connections 1000 --duration 10 --threads 4"
    echo "  $0 timer --duration 30                                  # 定时器专项"
    echo "  $0 eventloop --threads 4 --duration 10                  # EventLoop 专项"
    echo ""
}

# ============================================================
# 确保已编译
# ============================================================
ensure_built() {
    if [ ! -x "$BINARY" ]; then
        warn "Test_net 尚未编译，正在自动编译..."
        build
    fi
}

# ============================================================
# Main
# ============================================================
COMMAND="${1:-unit}"
shift 2>/dev/null || true

case "$COMMAND" in
    build)
        build
        ;;
    help|--help|-h)
        show_help
        ;;
    modules)
        show_modules
        ;;
    unit)
        ensure_built
        info "运行全部单元测试..."
        "$BINARY" --mode unit "$@"
        ;;
    server)
        ensure_built
        info "启动 Echo 服务器..."
        "$BINARY" --mode server "$@"
        ;;
    client)
        ensure_built
        info "启动压力客户端..."
        "$BINARY" --mode client "$@"
        ;;
    timer)
        ensure_built
        info "启动 HeapTimer 专项测试..."
        "$BINARY" --mode timer "$@"
        ;;
    eventloop)
        ensure_built
        info "启动 EventLoop 专项测试..."
        "$BINARY" --mode eventloop "$@"
        ;;
    benchmark)
        ensure_built
        info "启动全栈基准测试..."
        "$BINARY" --mode benchmark "$@"
        ;;
    list)
        ensure_built
        "$BINARY" --mode list
        ;;
    *)
        err "未知命令: $COMMAND"
        show_help
        exit 1
        ;;
esac
