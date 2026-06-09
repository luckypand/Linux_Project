/**
 * @file test_rpc_benchmark.cpp
 * @brief 多线程 RPC 客户端压测脚本
 *
 * 功能:
 *   - benchmark 模式: 内嵌 RPC 服务器 + 多线程客户端，完成 N 次同步 RPC 调用
 *   - server 模式:   独立 RPC 服务器，配合外部客户端或 perf 采样
 *
 * 测试场景:
 *   本机 (127.0.0.1) 发起 10 万次连续 RPC 同步调用 (MathService.Add)
 *   达标红线: 零半包/魔数校验失败，单次 RTT < 500 μs
 *
 * 用法:
 *   ./test_rpc_benchmark                           # 默认 benchmark 模式
 *   ./test_rpc_benchmark --mode server              # 独立服务器模式 (用于 perf 采样)
 *   ./test_rpc_benchmark --threads 4 --calls 50000  # 自定义线程数和调用次数
 *   ./test_rpc_benchmark --help                     # 帮助
 */

#include "RpcServer.hpp"
#include "RpcServiceAdapter.hpp"
#include "MathServiceImpl.hpp"
#include "RpcLatencyStats.hpp"
#include "../../proto/rpc_message.pb.h"
#include "../../proto/math_service.pb.h"
#include "my_log.hpp"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <cassert>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>

using namespace std;

// Get current steady_clock timestamp in microseconds
static inline uint64_t now_us() {
    return static_cast<uint64_t>(
        chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now().time_since_epoch()).count());
}

// ============================================================
// 全局信号标志 (server 模式用)
// ============================================================
static atomic<int> g_running{1};

static void on_signal(int) {
    g_running.store(0);
}

// ============================================================
// 命令行参数
// ============================================================
struct Args {
    string mode      = "benchmark";   // "benchmark" | "server"
    uint16_t port    = 8080;
    int threads      = 0;             // 0 = hardware_concurrency
    int calls        = 100000;
    double maxRttUs  = 500.0;         // 达标红线 (μs)
    string serverIp  = "127.0.0.1";
};

static Args g_args;

static void print_usage(const char* prog) {
    cout << "用法: " << prog << " [选项]\n"
         << "选项:\n"
         << "  --mode <mode>        运行模式: benchmark (默认) | server\n"
         << "  --port <port>        服务器端口 (默认: 8080)\n"
         << "  --threads <N>        客户端线程数 (默认: hardware_concurrency)\n"
         << "  --calls <N>          总 RPC 调用次数 (默认: 100000)\n"
         << "  --max-rtt-us <us>    RTT 达标红线 / 微秒 (默认: 500.0)\n"
         << "  --server-ip <ip>     服务器 IP (默认: 127.0.0.1)\n"
         << "  --help               打印此帮助\n"
         << "\n示例:\n"
         << "  " << prog << "                                    # 默认 benchmark\n"
         << "  " << prog << " --mode server                       # 独立服务器\n"
         << "  " << prog << " --threads 4 --calls 50000 --max-rtt-us 600\n"
         << flush;
}

static bool parse_args(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return false;
        } else if (arg == "--mode") {
            if (++i < argc) g_args.mode = argv[i];
        } else if (arg == "--port") {
            if (++i < argc) g_args.port = static_cast<uint16_t>(stoi(argv[i]));
        } else if (arg == "--threads") {
            if (++i < argc) g_args.threads = stoi(argv[i]);
        } else if (arg == "--calls") {
            if (++i < argc) g_args.calls = stoi(argv[i]);
        } else if (arg == "--max-rtt-us") {
            if (++i < argc) g_args.maxRttUs = stod(argv[i]);
        } else if (arg == "--server-ip") {
            if (++i < argc) g_args.serverIp = argv[i];
        } else {
            cerr << "未知参数: " << arg << "\n";
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

// ============================================================
// 可靠接收: 循环读取直到收满 n 字节
// ============================================================
static void recv_n(int fd, void* buf, size_t n) {
    size_t remaining = n;
    auto* pos = static_cast<char*>(buf);
    while (remaining > 0) {
        ssize_t r = ::recv(fd, pos, remaining, MSG_WAITALL);
        if (r == 0) {
            throw runtime_error("对端关闭连接 (收到 0 字节)");
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            throw runtime_error(string("recv 错误: ") + strerror(errno));
        }
        remaining -= static_cast<size_t>(r);
        pos += r;
    }
}

// ============================================================
// 可靠发送: 循环写入直到发完 n 字节
// ============================================================
static void send_n(int fd, const void* buf, size_t n) {
    size_t remaining = n;
    const auto* pos = static_cast<const char*>(buf);
    while (remaining > 0) {
        ssize_t w = ::send(fd, pos, remaining, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            throw runtime_error(string("send 错误: ") + strerror(errno));
        }
        remaining -= static_cast<size_t>(w);
        pos += w;
    }
}

// ============================================================
// 构建完整的发送数据包 (TLV header + protobuf payload)
// ============================================================
static string build_packet(const string& payload) {
    constexpr uint32_t MAGIC = 0x42414E41;  // "BANA"

    uint32_t be32_magic = htonl(MAGIC);
    uint32_t be32_len   = htonl(static_cast<uint32_t>(payload.size()));

    string packet;
    packet.reserve(8 + payload.size());
    packet.append(reinterpret_cast<const char*>(&be32_magic), 4);
    packet.append(reinterpret_cast<const char*>(&be32_len),   4);
    packet.append(payload);
    return packet;
}

// ============================================================
// 单线程统计数据
// ============================================================
struct ThreadStats {
    vector<double> rttUs;       // 每次调用的 RTT (微秒)

    // ★ 阶段分解：与 rttUs 一一对应的并行数组，用于 P99 尾部分析
    vector<double> upNetUs;     // 上行网络延迟 (client→server)
    vector<double> srvProcUs;   // 服务端处理时间
    vector<double> downNetUs;   // 下行网络延迟 (server→client)

    // ★ 时序采样：每 100 次请求记录一个 (elapsedSec, rttUs) 点
    vector<pair<double, double>> timeSeries;

    size_t calls   = 0;         // 成功调用次数
    size_t errors  = 0;         // 失败次数
    string  errorMsg;           // 首个错误描述

    // 超时请求耗时分解 (累计值，用于汇总)
    size_t timeoutCount      = 0;
    double timeoutUpNetSum   = 0.0;   // 上行网络延迟累加 (client→server)
    double timeoutSrvProcSum = 0.0;   // 服务端处理时间累加
    double timeoutDownNetSum = 0.0;   // 下行网络延迟累加 (server→client)
    double timeoutRttSum     = 0.0;   // 超时 RTT 累加
};

// ============================================================
// 客户端线程主函数
// ============================================================
static ThreadStats run_client_thread(int threadId, int numCalls,
                                     shared_future<void> startSignal,
                                     chrono::steady_clock::time_point benchStart) {
    ThreadStats stats;
    stats.rttUs.reserve(static_cast<size_t>(numCalls));
    stats.upNetUs.reserve(static_cast<size_t>(numCalls));
    stats.srvProcUs.reserve(static_cast<size_t>(numCalls));
    stats.downNetUs.reserve(static_cast<size_t>(numCalls));

    // ---- 重用 protobuf 对象，避免每轮分配 ----
    CoreX::rpc::MathRequest  mathReq;
    CoreX::rpc::MathResponse mathResp;
    CoreX::rpc::RpcMessage   rpcReq;
    CoreX::rpc::RpcMessage   rpcResp;

    string innerReqPayload;    // MathRequest 序列化结果
    string wireReqPayload;     // RpcMessage 序列化结果
    string wirePacket;         // 完整发送包

    // 预填充不变的字段
    rpcReq.set_type(CoreX::rpc::REQUEST);
    rpcReq.set_service("CoreX.rpc.MathService");
    rpcReq.set_method("Add");

    // ---- 建立 TCP 连接 ----
    int sock = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0) {
        stats.errors = 1;
        stats.errorMsg = "socket() 失败: " + string(strerror(errno));
        return stats;
    }

    // TCP_NODELAY: 禁用 Nagle，降低延迟
    int one = 1;
    ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(g_args.port);
    if (::inet_pton(AF_INET, g_args.serverIp.c_str(), &addr.sin_addr) != 1) {
        ::close(sock);
        stats.errors = 1;
        stats.errorMsg = "inet_pton() 失败";
        return stats;
    }

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr),
                  sizeof(addr)) < 0) {
        ::close(sock);
        stats.errors = 1;
        stats.errorMsg = "connect() 失败: " + string(strerror(errno));
        return stats;
    }

    // ---- 等待发令枪 ----
    startSignal.wait();

    // ---- 执行同步 RPC 调用 ----
    for (int i = 0; i < numCalls; ++i) {
        try {
            // 1. 构建 MathRequest
            mathReq.set_a(i + threadId * 1000000);
            mathReq.set_b(i + threadId * 1000000 + 1);
            int expectedResult = mathReq.a() + mathReq.b();

            // 2. 序列化 MathRequest → inner payload
            if (!mathReq.SerializeToString(&innerReqPayload)) {
                stats.errors++;
                if (stats.errorMsg.empty())
                    stats.errorMsg = "MathRequest 序列化失败";
                break;
            }

            // 3. 构建 RpcMessage 信封
            rpcReq.set_id(static_cast<uint64_t>(threadId) << 48 |
                          static_cast<uint64_t>(i));
            rpcReq.set_payload(innerReqPayload);
            rpcReq.set_client_send_ts(now_us());

            // 4. 序列化信封
            if (!rpcReq.SerializeToString(&wireReqPayload)) {
                stats.errors++;
                if (stats.errorMsg.empty())
                    stats.errorMsg = "RpcMessage 序列化失败";
                break;
            }

            // 5. 构建完整数据包
            wirePacket = build_packet(wireReqPayload);

            // 6. 计时开始 → 发送 → 接收 → 计时结束
            auto t1 = chrono::steady_clock::now();

            send_n(sock, wirePacket.data(), wirePacket.size());

            // 接收响应头部 (8 字节)
            uint8_t header[8];
            recv_n(sock, header, 8);

            // 解析魔数
            uint32_t recvMagic;
            memcpy(&recvMagic, header, 4);
            recvMagic = ntohl(recvMagic);
            if (recvMagic != 0x42414E41) {
                stats.errors++;
                if (stats.errorMsg.empty()) {
                    ostringstream oss;
                    oss << "魔数校验失败: 期望 0x42414E41, 收到 0x"
                        << hex << setfill('0') << setw(8) << recvMagic << dec;
                    stats.errorMsg = oss.str();
                }
                break;
            }

            // 解析 payload 长度
            uint32_t respLen;
            memcpy(&respLen, header + 4, 4);
            respLen = ntohl(respLen);

            if (respLen == 0 || respLen > 64 * 1024 * 1024) {
                stats.errors++;
                if (stats.errorMsg.empty()) {
                    ostringstream oss;
                    oss << "非法响应长度: " << respLen;
                    stats.errorMsg = oss.str();
                }
                break;
            }

            // 接收响应 payload
            string respPayload(respLen, '\0');
            recv_n(sock, &respPayload[0], respLen);

            auto t2 = chrono::steady_clock::now();

            // 7. 解析 RpcMessage
            if (!rpcResp.ParseFromString(respPayload)) {
                stats.errors++;
                if (stats.errorMsg.empty())
                    stats.errorMsg = "RpcMessage 反序列化失败";
                break;
            }

            // 8. 校验响应类型
            if (rpcResp.type() != CoreX::rpc::RESPONSE) {
                stats.errors++;
                if (stats.errorMsg.empty()) {
                    ostringstream oss;
                    oss << "非预期响应类型: " << rpcResp.type();
                    stats.errorMsg = oss.str();
                }
                break;
            }

            // 9. 解析 MathResponse
            if (!mathResp.ParseFromString(rpcResp.payload())) {
                stats.errors++;
                if (stats.errorMsg.empty())
                    stats.errorMsg = "MathResponse 反序列化失败";
                break;
            }

            // 10. 校验业务结果
            if (!mathResp.success()) {
                stats.errors++;
                if (stats.errorMsg.empty())
                    stats.errorMsg = "MathService 返回 success=false";
                break;
            }
            if (mathResp.result() != expectedResult) {
                stats.errors++;
                if (stats.errorMsg.empty()) {
                    ostringstream oss;
                    oss << "结果错误: 期望 " << expectedResult
                        << ", 实际 " << mathResp.result();
                    stats.errorMsg = oss.str();
                }
                break;
            }

            // 11. 记录 RTT 和时间戳分解
            double rtt = chrono::duration<double, micro>(t2 - t1).count();
            stats.rttUs.push_back(rtt);
            stats.calls++;

            // ★ 阶段分解：每个请求都记录，用于 P99 尾部分析
            {
                uint64_t client_send = rpcResp.client_send_ts();
                uint64_t server_recv = rpcResp.server_recv_ts();
                uint64_t server_send = rpcResp.server_send_ts();

                double upNet   = static_cast<double>(server_recv - client_send);
                double srvProc = static_cast<double>(server_send - server_recv);
                double downNet = rtt - upNet - srvProc;

                stats.upNetUs.push_back(upNet);
                stats.srvProcUs.push_back(srvProc);
                stats.downNetUs.push_back(downNet);

                // 当时序采样：每 100 次记录一个点
                if (stats.calls % 100 == 1) {
                    double elapsed = chrono::duration<double>(
                        chrono::steady_clock::now() - benchStart).count();
                    stats.timeSeries.push_back({elapsed, rtt});
                }
            }

            // 当 RTT 超过阈值时，用时间戳分解耗时
            if (rtt >= g_args.maxRttUs) {
                uint64_t client_send = rpcResp.client_send_ts();
                uint64_t server_recv = rpcResp.server_recv_ts();
                uint64_t server_send = rpcResp.server_send_ts();

                // 上行网络延迟: client发送 → server接收
                double upNet = static_cast<double>(server_recv - client_send);
                // 服务端处理: server接收 → server发送
                double srvProc = static_cast<double>(server_send - server_recv);
                // 下行网络延迟: server发送 → client完整接收
                double downNet = rtt - upNet - srvProc;

                stats.timeoutCount++;
                stats.timeoutUpNetSum   += upNet;
                stats.timeoutSrvProcSum += srvProc;
                stats.timeoutDownNetSum += downNet;
                stats.timeoutRttSum     += rtt;
            }

        } catch (const exception& e) {
            stats.errors++;
            if (stats.errorMsg.empty())
                stats.errorMsg = e.what();
            break;
        }
    }

    ::close(sock);
    return stats;
}

// ============================================================
// 统计计算
// ============================================================
struct BenchResult {
    size_t totalCalls   = 0;
    size_t totalErrors  = 0;
    double wallSec      = 0.0;
    double throughput   = 0.0;
    double minUs        = 0.0;
    double maxUs        = 0.0;
    double avgUs        = 0.0;
    double p50Us        = 0.0;
    double p90Us        = 0.0;
    double p95Us        = 0.0;
    double p99Us        = 0.0;
    double p999Us       = 0.0;
    size_t exceedCount  = 0;    // 超过 maxRttUs 的样本数
    bool   pass         = false;

    // ★ P99 尾部延迟分解
    size_t tailCount        = 0;     // 进入 P99 尾部的样本数
    double tailAvgUpNetUs   = 0.0;   // 尾部上行网络平均
    double tailAvgSrvProcUs = 0.0;   // 尾部服务端处理平均
    double tailAvgDownNetUs = 0.0;   // 尾部下行网络平均
    double tailAvgRttUs     = 0.0;   // 尾部 RTT 平均

    // ★ 延迟分布形态指标
    double p50Us_p99Us_ratio  = 0.0;  // P99 / P50 放大比
    double p50Us_p999Us_ratio = 0.0;  // P999 / P50 放大比

    // 超时请求耗时分解汇总
    size_t timeoutTotal          = 0;
    double timeoutAvgUpNetUs     = 0.0;
    double timeoutAvgSrvProcUs   = 0.0;
    double timeoutAvgDownNetUs   = 0.0;
    double timeoutAvgRttUs       = 0.0;
};

static BenchResult compute_stats(vector<ThreadStats>& allStats,
                                  double wallSec, double maxRttUs) {
    BenchResult result;
    result.wallSec = wallSec;

    // 合并所有线程的 RTT 数据和阶段分解
    vector<double> allRtt;
    vector<double> allUpNet;
    vector<double> allSrvProc;
    vector<double> allDownNet;
    for (auto& ts : allStats) {
        result.totalCalls  += ts.calls;
        result.totalErrors += ts.errors;
        allRtt.insert(allRtt.end(), ts.rttUs.begin(), ts.rttUs.end());
        allUpNet.insert(allUpNet.end(), ts.upNetUs.begin(), ts.upNetUs.end());
        allSrvProc.insert(allSrvProc.end(), ts.srvProcUs.begin(), ts.srvProcUs.end());
        allDownNet.insert(allDownNet.end(), ts.downNetUs.begin(), ts.downNetUs.end());
    }

    if (allRtt.empty()) {
        return result;
    }

    // ★ 保留未排序副本用于阶段分解（排序会打乱下标对应关系）
    vector<double> unsortedRtt = allRtt;

    // 排序（用于分位数计算）
    sort(allRtt.begin(), allRtt.end());

    size_t n = allRtt.size();
    result.minUs  = allRtt.front();
    result.maxUs  = allRtt.back();
    result.avgUs  = accumulate(allRtt.begin(), allRtt.end(), 0.0) / n;
    result.p50Us  = allRtt[n * 50  / 100];
    result.p90Us  = allRtt[n * 90  / 100];
    result.p95Us  = allRtt[n * 95  / 100];
    result.p99Us  = allRtt[n * 99  / 100];
    result.p999Us = allRtt[n * 999 / 1000];

    // ★ P99 尾部阶段分解：用未排序数组扫描 ≥ P99 的样本
    {
        double tailUpSum = 0.0, tailSrvSum = 0.0, tailDownSum = 0.0, tailRttSum = 0.0;
        result.tailCount = 0;
        double p99threshold = result.p99Us;
        for (size_t i = 0; i < unsortedRtt.size(); ++i) {
            if (unsortedRtt[i] >= p99threshold) {
                result.tailCount++;
                if (i < allUpNet.size())   tailUpSum   += allUpNet[i];
                if (i < allSrvProc.size()) tailSrvSum  += allSrvProc[i];
                if (i < allDownNet.size()) tailDownSum += allDownNet[i];
                tailRttSum += unsortedRtt[i];
            }
        }
        if (result.tailCount > 0) {
            result.tailAvgUpNetUs   = tailUpSum   / result.tailCount;
            result.tailAvgSrvProcUs = tailSrvSum  / result.tailCount;
            result.tailAvgDownNetUs = tailDownSum / result.tailCount;
            result.tailAvgRttUs     = tailRttSum  / result.tailCount;
        }
        // 尾延迟放大比
        if (result.p50Us > 0.0) {
            result.p50Us_p99Us_ratio  = result.p99Us  / result.p50Us;
            result.p50Us_p999Us_ratio = result.p999Us / result.p50Us;
        }
    }

    result.throughput = (wallSec > 0.0)
                        ? (static_cast<double>(result.totalCalls) / wallSec)
                        : 0.0;

    // 超标计数
    for (auto v : allRtt) {
        if (v >= maxRttUs) result.exceedCount++;
    }

    // 汇总超时请求的耗时分解
    for (auto& ts : allStats) {
        result.timeoutTotal += ts.timeoutCount;
    }
    if (result.timeoutTotal > 0) {
        double upSum = 0.0, srvSum = 0.0, downSum = 0.0, rttSum = 0.0;
        for (auto& ts : allStats) {
            upSum   += ts.timeoutUpNetSum;
            srvSum  += ts.timeoutSrvProcSum;
            downSum += ts.timeoutDownNetSum;
            rttSum  += ts.timeoutRttSum;
        }
        result.timeoutAvgUpNetUs   = upSum   / result.timeoutTotal;
        result.timeoutAvgSrvProcUs = srvSum  / result.timeoutTotal;
        result.timeoutAvgDownNetUs = downSum / result.timeoutTotal;
        result.timeoutAvgRttUs     = rttSum  / result.timeoutTotal;
    }

    // 达标判定
    result.pass = (result.totalErrors == 0) &&
                  (result.exceedCount == 0)   &&
                  (result.totalCalls > 0);

    return result;
}

// ============================================================
// 打印统计结果
// ============================================================
static void print_results(const BenchResult& r,
                          RpcLatencyStats* serverStats = nullptr) {
    cout << "\n";
    cout << "======================== RPC Benchmark Results ========================\n";
    cout << "  Server:             " << g_args.serverIp << ":" << g_args.port << "\n";
    cout << "  Threads:            " << g_args.threads << "\n";
    cout << "  Total calls:        " << (r.totalCalls + r.totalErrors) << "\n";
    cout << "  Successful:         " << r.totalCalls << "\n";
    cout << "  Errors:             " << r.totalErrors << "\n";

    double errRate = 0.0;
    size_t total = r.totalCalls + r.totalErrors;
    if (total > 0) errRate = 100.0 * r.totalErrors / total;
    cout << "  Error rate:         " << fixed << setprecision(3)
         << errRate << "%\n";

    cout << "  Wall clock:         " << fixed << setprecision(3)
         << r.wallSec << " s\n";
    cout << "  Throughput:         " << fixed << setprecision(0)
         << r.throughput << " calls/s\n";

    cout << "\n  Latency (microseconds):\n";
    cout << "    Minimum:          " << fixed << setprecision(2) << r.minUs  << "\n";
    cout << "    Average:          " << fixed << setprecision(2) << r.avgUs  << "\n";
    cout << "    Maximum:          " << fixed << setprecision(2) << r.maxUs  << "\n";
    cout << "    Median (P50):     " << fixed << setprecision(2) << r.p50Us  << "\n";
    cout << "    P90:              " << fixed << setprecision(2) << r.p90Us  << "\n";
    cout << "    P95:              " << fixed << setprecision(2) << r.p95Us  << "\n";
    cout << "    P99:              " << fixed << setprecision(2) << r.p99Us  << "\n";
    cout << "    P99.9:            " << fixed << setprecision(2) << r.p999Us << "\n";

    // ★ 尾延迟放大比
    if (r.p50Us > 0.0) {
        cout << "\n  Tail latency amplification:\n";
        cout << "    P99  / P50:       " << fixed << setprecision(1)
             << r.p50Us_p99Us_ratio << "x";
        if (r.p50Us_p99Us_ratio > 10.0) {
            cout << "  ⚠ 严重长尾 (>10x)";
        } else if (r.p50Us_p99Us_ratio > 3.0) {
            cout << "  ⚠ 明显长尾 (>3x)";
        } else {
            cout << "  ✓ 正常";
        }
        cout << "\n";
        cout << "    P999 / P50:       " << fixed << setprecision(1)
             << r.p50Us_p999Us_ratio << "x";
        if (r.p50Us_p999Us_ratio > 50.0) {
            cout << "  ⚠ P999 严重长尾 (>50x)";
        } else if (r.p50Us_p999Us_ratio > 10.0) {
            cout << "  ⚠ P999 明显长尾 (>10x)";
        } else {
            cout << "  ✓ 正常";
        }
        cout << "\n";
    }

    // ★ P99 尾部延迟分解（找出尾延迟瓶颈）
    if (r.tailCount > 0) {
        double tailTotal = r.tailAvgUpNetUs + r.tailAvgSrvProcUs + r.tailAvgDownNetUs;
        if (tailTotal <= 0.0) tailTotal = 1.0;
        cout << "\n  ╔══ P99 尾部延迟分解 (≥ " << fixed << setprecision(1) << r.p99Us
             << " us, 共 " << r.tailCount << " 样本) ══╗\n";
        cout << "  ║ 上行网络 (client→server):  "
             << setw(8) << fixed << setprecision(1) << r.tailAvgUpNetUs   << " us  ("
             << setw(5) << setprecision(1) << (100.0 * r.tailAvgUpNetUs / tailTotal) << "%)"
             << "         ║\n";
        cout << "  ║ 服务端处理:                "
             << setw(8) << fixed << setprecision(1) << r.tailAvgSrvProcUs << " us  ("
             << setw(5) << setprecision(1) << (100.0 * r.tailAvgSrvProcUs / tailTotal) << "%)"
             << "         ║\n";
        cout << "  ║ 下行网络 (server→client):  "
             << setw(8) << fixed << setprecision(1) << r.tailAvgDownNetUs << " us  ("
             << setw(5) << setprecision(1) << (100.0 * r.tailAvgDownNetUs / tailTotal) << "%)"
             << "         ║\n";
        cout << "  ╠══════════════════════════════════════════════════════════════╣\n";
        cout << "  ║ ⚡ P99 尾延迟主因: ";
        if (r.tailAvgSrvProcUs >= r.tailAvgUpNetUs &&
            r.tailAvgSrvProcUs >= r.tailAvgDownNetUs) {
            cout << "服务端处理                              ║\n";
            cout << "  ║    → 建议: 增加工作线程 / 优化 dispatch / 减少锁竞争     ║\n";
        } else if (r.tailAvgUpNetUs >= r.tailAvgDownNetUs) {
            cout << "上行网络延迟                            ║\n";
            cout << "  ║    → 建议: 检查客户端发送队列 / TCP 拥塞窗口             ║\n";
        } else {
            cout << "下行网络延迟                            ║\n";
            cout << "  ║    → 建议: 检查服务端 send 缓冲 / 客户端 recv 效率      ║\n";
        }
        cout << "  ╚══════════════════════════════════════════════════════════════╝\n";
    }

    cout << "\n  Threshold check (" << fixed << setprecision(1)
         << g_args.maxRttUs << " μs):\n";
    cout << "    Samples above:    " << r.exceedCount;
    if (r.totalCalls + r.totalErrors > 0) {
        cout << " (" << fixed << setprecision(3)
             << (100.0 * r.exceedCount / (r.totalCalls + r.totalErrors))
             << "%)";
    }
    cout << "\n    Max observed:     " << fixed << setprecision(2)
         << r.maxUs << " μs\n";

    // 超时请求耗时分解汇总
    if (r.timeoutTotal > 0) {
        double totalAvg = r.timeoutAvgUpNetUs + r.timeoutAvgSrvProcUs + r.timeoutAvgDownNetUs;
        cout << "\n  超时请求耗时分解 (共 " << r.timeoutTotal << " 次, 平均值):\n";
        cout << "    上行网络 (client→server):  "
             << setw(8) << fixed << setprecision(1) << r.timeoutAvgUpNetUs   << " us  ("
             << setprecision(1) << (100.0 * r.timeoutAvgUpNetUs / totalAvg) << "%)\n";
        cout << "    服务端处理:                "
             << setw(8) << fixed << setprecision(1) << r.timeoutAvgSrvProcUs << " us  ("
             << setprecision(1) << (100.0 * r.timeoutAvgSrvProcUs / totalAvg) << "%)\n";
        cout << "    下行网络 (server→client):  "
             << setw(8) << fixed << setprecision(1) << r.timeoutAvgDownNetUs << " us  ("
             << setprecision(1) << (100.0 * r.timeoutAvgDownNetUs / totalAvg) << "%)\n";
        cout << "    超时平均 RTT:              "
             << setw(8) << fixed << setprecision(1) << r.timeoutAvgRttUs     << " us\n";

        // 判断主要瓶颈
        cout << "    ⚡ 超时主因: ";
        if (r.timeoutAvgSrvProcUs >= r.timeoutAvgUpNetUs &&
            r.timeoutAvgSrvProcUs >= r.timeoutAvgDownNetUs) {
            cout << "服务端处理耗时占比最高 (" << fixed << setprecision(0)
                 << (100.0 * r.timeoutAvgSrvProcUs / totalAvg) << "%), "
                 << "建议优化服务端逻辑或增加工作线程\n";
        } else if (r.timeoutAvgUpNetUs >= r.timeoutAvgDownNetUs) {
            cout << "上行网络延迟占比最高 (" << fixed << setprecision(0)
                 << (100.0 * r.timeoutAvgUpNetUs / totalAvg) << "%), "
                 << "建议检查客户端发送队列或网络拥塞\n";
        } else {
            cout << "下行网络延迟占比最高 (" << fixed << setprecision(0)
                 << (100.0 * r.timeoutAvgDownNetUs / totalAvg) << "%), "
                 << "建议检查服务端发送队列或客户端接收效率\n";
        }
    }

    // ★ 服务端延迟统计（如果已启用）
    if (serverStats && serverStats->totalCount() > 0) {
        cout << "\n";
        serverStats->printSummary(cout);
        serverStats->printBreakdown(cout);
    }

    cout << "\n  Result:             "
         << (r.pass ? "\033[1;32mPASS\033[0m" : "\033[1;31mFAIL\033[0m")
         << "\n";
    cout << "======================================================================\n";

    if (!r.pass) {
        cout << "\n[FAIL] 原因:\n";
        if (r.totalErrors > 0)
            cout << "  - 发生 " << r.totalErrors << " 次协议/业务错误\n";
        if (r.exceedCount > 0)
            cout << "  - " << r.exceedCount << " 次 RTT 超过 "
                 << g_args.maxRttUs << " μs 红线\n";
        if (r.totalCalls == 0)
            cout << "  - 无成功调用\n";
    }
    cout << flush;
}

// ============================================================
// ★ 预热函数：发送若干次 RPC 调用，消除冷启动效应
//    JIT 编译、CPU 缓存预热、TCP slow-start 等
// ============================================================
static void warmup_client(int numWarmup) {
    int sock = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0) return;

    int one = 1;
    ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(g_args.port);
    if (::inet_pton(AF_INET, g_args.serverIp.c_str(), &addr.sin_addr) != 1) {
        ::close(sock);
        return;
    }
    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        return;
    }

    CoreX::rpc::MathRequest  mathReq;
    CoreX::rpc::RpcMessage   rpcReq;
    rpcReq.set_type(CoreX::rpc::REQUEST);
    rpcReq.set_service("CoreX.rpc.MathService");
    rpcReq.set_method("Add");

    string innerReqPayload, wireReqPayload, wirePacket, respPayload;
    uint8_t header[8];

    for (int i = 0; i < numWarmup; ++i) {
        try {
            mathReq.set_a(i);
            mathReq.set_b(i + 1);
            mathReq.SerializeToString(&innerReqPayload);
            rpcReq.set_id(static_cast<uint64_t>(i));
            rpcReq.set_payload(innerReqPayload);
            rpcReq.set_client_send_ts(now_us());
            rpcReq.SerializeToString(&wireReqPayload);
            wirePacket = build_packet(wireReqPayload);

            send_n(sock, wirePacket.data(), wirePacket.size());
            recv_n(sock, header, 8);

            uint32_t respLen;
            memcpy(&respLen, header + 4, 4);
            respLen = ntohl(respLen);
            if (respLen > 0 && respLen <= 64 * 1024 * 1024) {
                respPayload.resize(respLen);
                recv_n(sock, &respPayload[0], respLen);
            }
        } catch (...) {
            break;  // 预热中出现任何错误都直接退出
        }
    }

    ::close(sock);
}

// ============================================================
// Benchmark 模式: 内嵌服务器 + 多线程客户端
// ============================================================
static int run_benchmark(RpcLatencyStats* outServerStats = nullptr) {
    if (g_args.threads <= 0) {
        g_args.threads = static_cast<int>(thread::hardware_concurrency());
        if (g_args.threads <= 0) g_args.threads = 2;
    }
    if (g_args.calls <= 0) {
        cerr << "错误: --calls 必须 > 0\n";
        return 1;
    }

    cout << "[benchmark] 配置:\n";
    cout << "  服务器:    " << g_args.serverIp << ":" << g_args.port << "\n";
    cout << "  线程数:    " << g_args.threads << "\n";
    cout << "  总调用:    " << g_args.calls << "\n";
    cout << "  RTT 红线:  " << g_args.maxRttUs << " μs\n";
    cout << flush;

    // ---- 1. 启动内嵌服务器 ----
    cout << "\n[benchmark] 启动内嵌 RPC 服务器...\n";
    MathServiceImpl mathService;

    // ★ 创建服务端延迟统计器
    RpcLatencyStats serverStats("RpcServer-Internal", 1'000'000, 0.1, 10'000.0, 50);

    auto readyPromise = make_shared<promise<void>>();
    future<void> readyFuture = readyPromise->get_future();

    atomic<EventLoop*> serverLoop{nullptr};

    thread serverThread([&]() {
        EventLoop loop;
        serverLoop.store(&loop);
        RpcServer server(&loop, g_args.serverIp, g_args.port, "BenchServer");
        server.registerService(&mathService);
        server.setLatencyStats(&serverStats);  // ★ 启用服务端统计收集
        server.start();

        cout << "[server] RPC 服务器已启动: " << g_args.serverIp
             << ":" << g_args.port << "\n";
        readyPromise->set_value();

        loop.loop();  // 阻塞直到 quit()
        cout << "[server] RPC 服务器已停止\n";
    });

    // 等待服务器就绪
    readyFuture.wait();

    // 给服务器一点额外时间完成 accept 绑定
    this_thread::sleep_for(chrono::milliseconds(100));

    // ★ 预热阶段：消除 JIT/Cache/TCP slow-start 冷启动效应
    if (g_args.calls > 10000) {
        cout << "[benchmark] 预热中 (1000 次调用)...\n";
        warmup_client(1000);
        cout << "[benchmark] 预热完成，开始正式测量\n\n";
    }

    // ---- 2. 启动客户端线程 ----
    cout << "[benchmark] 启动 " << g_args.threads << " 个客户端线程...\n";

    // 分配每线程调用数
    int callsPerThread = g_args.calls / g_args.threads;
    int remainder      = g_args.calls % g_args.threads;

    vector<int> threadCalls(static_cast<size_t>(g_args.threads), callsPerThread);
    for (int i = 0; i < remainder; ++i) {
        threadCalls[static_cast<size_t>(i)]++;
    }

    // 发令枪
    promise<void> startPromise;
    shared_future<void> startSignal = startPromise.get_future().share();

    // 记录基准时间（用于时序采样）
    auto benchStartTime = chrono::steady_clock::now();

    // 启动线程
    vector<future<ThreadStats>> futures;
    for (int i = 0; i < g_args.threads; ++i) {
        if (threadCalls[static_cast<size_t>(i)] <= 0) continue;
        futures.push_back(async(launch::async,
            [i, calls = threadCalls[static_cast<size_t>(i)], startSignal, benchStartTime]() {
                return run_client_thread(i, calls, startSignal, benchStartTime);
            }));
    }

    // 发令
    auto benchStart = chrono::steady_clock::now();
    startPromise.set_value();

    // 收集结果
    vector<ThreadStats> allStats;
    for (auto& fut : futures) {
        allStats.push_back(fut.get());
    }
    auto benchEnd = chrono::steady_clock::now();

    double wallSec = chrono::duration<double>(benchEnd - benchStart).count();

    // 打印每线程摘要
    cout << "\n[benchmark] 线程摘要:\n";
    for (size_t i = 0; i < allStats.size(); ++i) {
        auto& ts = allStats[i];
        cout << "  线程 " << i << ": " << ts.calls << " 成功";
        if (ts.errors > 0) {
            cout << ", " << ts.errors << " 错误 (" << ts.errorMsg << ")";
        }
        if (!ts.rttUs.empty()) {
            cout << ", 平均 RTT " << fixed << setprecision(1)
                 << (accumulate(ts.rttUs.begin(), ts.rttUs.end(), 0.0)
                     / ts.rttUs.size()) << " μs";
        }
        cout << "\n";
    }
    cout << flush;

    // ---- 3. 停止服务器 ----
    cout << "\n[benchmark] 停止服务器...\n";
    EventLoop* loop = serverLoop.load();
    if (loop) {
        loop->quit();
    }
    serverThread.join();

    // ---- 4. 计算并打印统计 ----
    BenchResult result = compute_stats(allStats, wallSec, g_args.maxRttUs);
    print_results(result, &serverStats);  // ★ 传入服务端统计

    // ★ 将服务端统计传出给调用者（用 merge 代替赋值，因为 mutex 不可拷贝）
    if (outServerStats) {
        outServerStats->merge(serverStats);
    }

    return result.pass ? 0 : 1;
}

// ============================================================
// Server 模式: 独立 RPC 服务器 (用于 perf 采样)
// ============================================================
static int run_server() {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    EventLoop loop;
    RpcServer server(&loop, "0.0.0.0", g_args.port, "RpcBenchServer");
    MathServiceImpl mathService;
    server.registerService(&mathService);
    server.start();

    cout << "[server] RPC Benchmark Server started on 0.0.0.0:"
         << g_args.port << "\n";
    cout << "[server] PID: " << getpid() << "\n";
    cout << "[server] 等待客户端连接... (Ctrl+C 停止)\n";
    cout << "[server] 可用 perf 采样: sudo perf record -F 99 -g --call-graph dwarf -p "
         << getpid() << "\n";
    cout << flush;

    // 监视线程: 周期性检查退出信号
    thread watcher([&]() {
        while (g_running.load()) {
            this_thread::sleep_for(chrono::milliseconds(200));
        }
        loop.quit();
    });

    loop.loop();

    g_running.store(0);
    watcher.join();

    cout << "[server] 服务器已正常退出\n";
    return 0;
}

// ============================================================
// main
// ============================================================
int main(int argc, char* argv[]) {
    // 验证 protobuf 版本兼容性
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // 初始化日志系统 (异步模式，capacity=1024，测试死锁修复)
    Log::Instance()->Init(LOG_INFO, "/root/Cplus/CoreX/log", 1024, ".log");

    if (!parse_args(argc, argv)) {
        return 1;
    }

    if (g_args.mode == "server") {
        return run_server();
    } else if (g_args.mode == "benchmark") {
        return run_benchmark();
    } else {
        cerr << "错误: 未知模式 '" << g_args.mode
             << "', 可选: benchmark | server\n";
        return 1;
    }
}
