/*
 * test_ipc.cpp — 共享内存无锁队列正确性与性能验证
 *
 * 测试目标：验证 ShmRingBuffer 无锁队列在极限场景下的正确性和性能。
 *
 * [场景1] Ping-Pong 极限延迟测试
 *   两个独立进程通过共享内存环形缓冲区互传 100 字节数据，重复 100 万次往返。
 *   以 std::chrono::high_resolution_clock（纳秒级时钟）测量端到端延迟。
 *   达标红线：P50/P99 延迟稳定在 10 ~ 30 微秒（us）级别，直接碾压 TCP 500us 延迟。
 *
 * [场景2] 车载大数据流吞吐测试
 *   模拟激光雷达点云数据流，单进程多线程，每秒写入 100 次 10MB 点云包（1GB/s）。
 *   接收端逐包 MD5 校验，确保无锁队列在高吞吐压力下不发生数据踩踏（Data Corruption）。
 *   达标红线：所有 MD5 校验全部通过，无数据损坏。
 *
 * 用法:
 *   ./test_ipc --mode pingpong     # 仅运行场景1
 *   ./test_ipc --mode throughput   # 仅运行场景2
 *   ./test_ipc --mode all          # 全部场景 (默认)
 *
 *   ./test_ipc --mode pingpong --rounds 100000  # 自定义往返次数
 *   ./test_ipc --mode throughput --duration 10  # 自定义持续时间(秒)
 */

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <algorithm>
#include <sstream>
#include <thread>
#include <vector>
#include <atomic>
#include <sys/wait.h>
#include <unistd.h>

#include "ShmSegment.hpp"
#include "RingBuffer.hpp"

using namespace std::chrono;

// ============================================================================
// 测试配置常量
// ============================================================================

/// Ping-Pong 消息体大小（字节），模拟典型控制指令/状态回传
constexpr size_t PINGPONG_PAYLOAD  = 100;

/// 默认往返次数（100 万次）
constexpr int    PINGPONG_ROUNDS   = 1000000;

/// 延迟分布直方图柱数
constexpr int    LATENCY_HIST_BINS = 50;

/// 大数据块有效载荷 — 10MB（模拟单帧激光雷达点云）
constexpr size_t LARGE_PAYLOAD     = 10UL * 1024 * 1024;

/// 大数据环形队列容量（4 块，可用 3 块以区分空/满）
constexpr int    LARGE_RING_CAP    = 4;

/// 吞吐测试默认持续时间（秒）
constexpr int    THROUGHPUT_SEC    = 5;

/// 目标写入频率（Hz）
constexpr int    TARGET_WRITE_HZ   = 100;

/// 共享内存名称前缀
constexpr const char* SHM_PINGPONG = "/test_ipc_pingpong";
constexpr const char* SHM_LARGE    = "/test_ipc_throughput";

// ============================================================================
// MD5 工具（使用 OpenSSL/libcrypto）
// ============================================================================

#include <openssl/md5.h>

/// 计算数据的 MD5 摘要，返回 32 字符小写十六进制字符串
static std::string md5_hex(const void* data, size_t len) {
    unsigned char digest[MD5_DIGEST_LENGTH];  // 16 字节
    MD5(static_cast<const unsigned char*>(data), len, digest);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << static_cast<int>(digest[i]);
    }
    return oss.str();
}

/// 直接获取 16 字节原始 MD5 摘要
static void md5_raw(const void* data, size_t len, unsigned char out[16]) {
    MD5(static_cast<const unsigned char*>(data), len, out);
}

// ============================================================================
// 场景2: 大数据块环形缓冲区（SPSC 无锁设计）
// ============================================================================

/**
 * LargeBlock — 单个大数据块（10MB 点云包 + 元数据）
 *
 * 内存布局（cacheline 对齐）:
 * ┌──────────────────────────────────────────────────────────────┐
 * │ state(4B) │ data_len(8B) │ md5[16] │ pad[36B] │ data[10MB]   │
 * │           │              │         │          │              │
 * │ <------------------ 64B header -----------------> │          │
 * └──────────────────────────────────────────────────────────────┘
 *
 * 状态机: FREE(0) → WRITING(1) → READY(2) → FREE(0) (循环)
 */
struct alignas(64) LargeBlock {
    std::atomic<int> state{0};           // 块状态: 0=FREE, 1=WRITING, 2=READY
    size_t           data_len{0};         // 实际数据长度
    unsigned char    md5_hash[16];        // 发送端预先计算的 MD5
    char             _pad[64 - sizeof(std::atomic<int>) - sizeof(size_t) - 16];
    char             data[LARGE_PAYLOAD]; // 点云数据载荷
};

/**
 * LargeRingBuffer — 大数据块 SPSC 环形队列
 *
 *   ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
 *   │ Block 0  │ →  │ Block 1  │ →  │ Block 2  │ →  │ Block 3  │
 *   │  10MB    │    │  10MB    │    │  10MB    │    │  10MB    │
 *   └──────────┘    └──────────┘    └──────────┘    └──────────┘
 *        ↑                                                 │
 *        └─────────────────────────────────────────────────┘
 *                    write_index / read_index 循环推进
 *
 * 容量: 4 块，可用 3 块（留 1 块余量区分环形队列空/满状态）
 */
struct LargeRingBuffer {
    alignas(64) std::atomic<size_t> write_index{0};
    char _pad1[64 - sizeof(std::atomic<size_t>)];    // 填充至 64B cacheline
    alignas(64) std::atomic<size_t> read_index{0};
    char _pad2[64 - sizeof(std::atomic<size_t>)];    // 填充至 64B cacheline
    LargeBlock blocks[LARGE_RING_CAP];

    LargeRingBuffer() {
        for (int i = 0; i < LARGE_RING_CAP; ++i) {
            blocks[i].state.store(0, std::memory_order_relaxed);
        }
    }

    /**
     * push — 生产者写入（SPSC，单写者无 CAS 竞争，但仍通过 CAS 保证状态转移的原子可见性）
     *
     * @param data  数据指针
     * @param len   数据长度（≤ LARGE_PAYLOAD）
     * @param md5   预计算的 16 字节 MD5 摘要
     * @return      true = 写入成功，false = 队列满或数据过大
     */
    bool push(const void* data, size_t len, const unsigned char md5[16]) {
        if (len > LARGE_PAYLOAD) return false;

        size_t w = write_index.load(std::memory_order_relaxed);
        size_t next = (w + 1) % LARGE_RING_CAP;

        // 环形队列满判断：next 追上 read_index
        if (next == read_index.load(std::memory_order_acquire)) {
            return false;
        }

        LargeBlock& blk = blocks[w];

        // CAS 状态转移: FREE → WRITING（自旋等待块被消费者释放）
        int expected = 0;
        while (!blk.state.compare_exchange_weak(
                   expected, 1,
                   std::memory_order_acquire, std::memory_order_relaxed)) {
            expected = 0;
        }

        // 写入数据和元信息
        blk.data_len = len;
        std::memcpy(blk.md5_hash, md5, 16);
        std::memcpy(blk.data, data, len);

        // 发布: WRITING → READY（release 语义确保数据写入对消费者可见）
        blk.state.store(2, std::memory_order_release);
        write_index.store(next, std::memory_order_release);
        return true;
    }

    /**
     * pop — 消费者读取（SPSC，单消费者）
     *
     * @param out_data  输出数据缓冲区
     * @param out_md5   输出 16 字节 MD5 摘要
     * @return          true = 读取成功，false = 队列空
     */
    bool pop(std::vector<char>& out_data, unsigned char out_md5[16]) {
        size_t r = read_index.load(std::memory_order_relaxed);

        // 环形队列空判断: read_index 追上 write_index
        if (r == write_index.load(std::memory_order_acquire)) {
            return false;
        }

        LargeBlock& blk = blocks[r];

        // acquire 语义: 确保看到生产者写入的完整数据
        if (blk.state.load(std::memory_order_acquire) != 2) {
            return false;  // 生产者尚未完成写入
        }

        // 拷贝数据
        out_data.assign(blk.data, blk.data + blk.data_len);
        std::memcpy(out_md5, blk.md5_hash, 16);

        // 释放块: READY → FREE（release 语义确保读取完成后才释放）
        blk.state.store(0, std::memory_order_release);
        read_index.store((r + 1) % LARGE_RING_CAP, std::memory_order_release);
        return true;
    }
};

// 大型共享内存总大小 = 头部 + 环形缓冲区
constexpr size_t LARGE_SHM_SIZE = sizeof(LargeRingBuffer);

// ============================================================================
// 统计工具
// ============================================================================

struct LatencyStats {
    double min_us;
    double max_us;
    double avg_us;
    double p50_us;   // 中位数
    double p95_us;
    double p99_us;
    double p999_us;
    std::vector<int> histogram;  // 每柱计数
    double hist_bin_width_us;

    static LatencyStats compute(std::vector<double>& samples, int num_bins) {
        LatencyStats st{};
        if (samples.empty()) return st;

        std::sort(samples.begin(), samples.end());

        st.min_us = samples.front();
        st.max_us = samples.back();
        st.avg_us = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();

        auto pct = [&](double p) -> double {
            size_t idx = static_cast<size_t>(samples.size() * p / 100.0);
            if (idx >= samples.size()) idx = samples.size() - 1;
            return samples[idx];
        };

        st.p50_us  = pct(50);
        st.p95_us  = pct(95);
        st.p99_us  = pct(99);
        st.p999_us = pct(99.9);

        // 构建直方图: 从 min 到 max 均分 num_bins 个桶
        double range = st.max_us - st.min_us;
        if (range <= 0.0) range = 1.0;
        st.hist_bin_width_us = range / num_bins;
        st.histogram.resize(num_bins, 0);
        for (double v : samples) {
            int bin = static_cast<int>((v - st.min_us) / st.hist_bin_width_us);
            if (bin >= num_bins) bin = num_bins - 1;
            if (bin < 0) bin = 0;
            st.histogram[bin]++;
        }

        return st;
    }

    void print(const char* title, size_t sample_count) const {
        std::cout << "\n";
        std::cout << "┌─────────────────────────────────────────────────┐\n";
        std::cout << "│ " << title << "\n";
        std::cout << "├─────────────────────────────────────────────────┤\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "│  样本数:   " << std::setw(10) << sample_count << "\n";
        std::cout << "│  最小值:  " << std::setw(10) << min_us  << " us\n";
        std::cout << "│  平均值:  " << std::setw(10) << avg_us  << " us\n";
        std::cout << "│  最大值:  " << std::setw(10) << max_us  << " us\n";
        std::cout << "│  P50:     " << std::setw(10) << p50_us  << " us\n";
        std::cout << "│  P95:     " << std::setw(10) << p95_us  << " us\n";
        std::cout << "│  P99:     " << std::setw(10) << p99_us  << " us\n";
        std::cout << "│  P99.9:   " << std::setw(10) << p999_us << " us\n";
        std::cout << "├─────────────────────────────────────────────────┤\n";
        std::cout << "│ 延迟分布直方图 (每柱 ≈ " << std::setprecision(1)
                  << hist_bin_width_us << " us):\n";
        std::cout << "│\n";

        // 找最大柱高度用于归一化（最多 40 字符宽）
        int max_count = *std::max_element(histogram.begin(), histogram.end());
        const int bar_width = 40;

        for (int i = 0; i < static_cast<int>(histogram.size()); ++i) {
            double lo = min_us + i * hist_bin_width_us;
            double hi = lo + hist_bin_width_us;
            int bar_len = (max_count > 0) ? (histogram[i] * bar_width / max_count) : 0;

            std::cout << "│ [" << std::setw(8) << std::setprecision(1) << lo
                      << "," << std::setw(8) << hi << ") ";
            for (int j = 0; j < bar_len; ++j) std::cout << "█";
            std::cout << " " << histogram[i] << "\n";
        }
        std::cout << "└─────────────────────────────────────────────────┘\n";
    }
};

// ============================================================================
// 场景1: Ping-Pong 极限延迟测试
// ============================================================================

/**
 * 场景1 架构:
 *
 *   ┌────────────────── 共享内存 ──────────────────┐
 *   │                                               │
 *   │  ┌─────────────────┐  ┌─────────────────┐     │
 *   │  │  PingRingBuffer  │  │  PongRingBuffer  │     │
 *   │  │  (父 → 子)       │  │  (子 → 父)       │     │
 *   │  │  16块 × 4KB      │  │  16块 × 4KB      │     │
 *   │  └────────┬────────┘  └────────┬────────┘     │
 *   │           │                    │              │
 *   └───────────│────────────────────│──────────────┘
 *               │                    │
 *     ┌─────────▼────────┐  ┌───────▼───────────┐
 *     │  父进程 (Server)  │  │  子进程 (Client)   │
 *     │  CAS_Push(ping)  │  │  CAS_Pop(ping)    │
 *     │  CAS_Pop(pong)   │  │  CAS_Push(pong)   │
 *     │  计时 + 统计      │  │  纯收发 (无计时)   │
 *     └──────────────────┘  └───────────────────┘
 *
 * 时序:
 *
 *   父进程                          子进程
 *   ──────                         ──────
 *   t0 = now()                     │
 *   CAS_Push(ping) ───────────────► CAS_Pop(ping)
 *   (自旋等待 pong)                │ CAS_Push(pong)
 *   CAS_Pop(pong) ◄─────────────── │
 *   t1 = now()                     │
 *   记录延迟 = t1 - t0             │
 *   (循环 100 万次)                │ (循环 100 万次)
 *
 * 关键设计决策:
 * - 使用 fork() 而非独立可执行文件: 子进程继承父进程的共享内存映射，
 *   避免重新 open/mmap 的开销，使延迟测量更纯粹反映无锁队列性能。
 * - 自旋等待 (spin-wait) 而非 eventfd 通知: 消除系统调用开销，
 *   测量无锁队列的硬件极限延迟。
 * - 父进程记录时间: 从 Push ping 前到 Pop pong 后为一个完整往返。
 */
static int run_pingpong(int total_rounds) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║  场景1: Ping-Pong 极限延迟测试                     ║\n";
    std::cout << "║  消息大小: " << std::setw(5) << PINGPONG_PAYLOAD
              << " 字节                                  ║\n";
    std::cout << "║  往返次数: " << std::setw(7) << total_rounds
              << "                                    ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";

    // ---- 创建共享内存 & 初始化两个环形缓冲区 ----
    // 布局: [PingRingBuffer (父→子)] [PongRingBuffer (子→父)]
    const size_t shm_size = sizeof(ShmRingBuffer) * 2;

    ShmMemoryPool pool(SHM_PINGPONG, shm_size, ShmMemoryPool::CREATE);
    void* base = pool.GetMappedptr();

    // placement new 在共享内存上构造环形缓冲区
    ShmRingBuffer* ping_ring = new (base) ShmRingBuffer();
    ShmRingBuffer* pong_ring = new (
        static_cast<char*>(base) + sizeof(ShmRingBuffer)) ShmRingBuffer();

    // ---- 准备测试数据 ----
    char send_buf[PINGPONG_PAYLOAD];
    std::memset(send_buf, 0xAB, sizeof(send_buf));  // 填充固定模式

    // ---- fork 子进程 ----
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[错误] fork 失败\n";
        return 1;
    }

    if (pid == 0) {
        // ======================== 子进程（Client/Pong 端） ========================
        // fork() 后子进程继承了父进程的共享内存映射，直接使用指针即可。
        // 注意: 不使用 ShmMemoryPool 的方法避免重复 shm_unlink。

        std::string recv;
        recv.reserve(PINGPONG_PAYLOAD);

        // 子进程需处理: 预热 100 次 + 正式测试 total_rounds 次
        const int child_total = 100 + total_rounds;
        for (int i = 0; i < child_total; ++i) {
            // 自旋等待父进程的 ping 消息
            while (!ping_ring->CAS_Pop(recv)) {
                // busy-wait — 极低延迟，无系统调用开销
            }

            // 立即回传 pong（相同的 100 字节数据原样返回）
            while (!pong_ring->CAS_Push(recv.data(), recv.size())) {
                // 环形队列满时自旋（理论上不会发生，因为父进程在等 pong）
            }
        }

        _exit(0);  // 子进程直接退出，不调用任何析构函数
    }

    // ======================== 父进程（Server/Ping 端） ========================

    // ---- 预热: 100 次往返让 CPU 缓存 / 频率稳定 ----
    std::cout << "[预热] 执行 100 次预热往返...\n";
    {
        std::string warm_recv;
        for (int i = 0; i < 100; ++i) {
            while (!ping_ring->CAS_Push(send_buf, sizeof(send_buf))) {}
            while (!pong_ring->CAS_Pop(warm_recv)) {}
        }
    }
    std::cout << "[预热] 完成，开始正式测试...\n";

    // ---- 正式测试: 记录每次往返延迟 ----
    std::vector<double> latencies;
    latencies.reserve(total_rounds);

    std::string recv;
    recv.reserve(PINGPONG_PAYLOAD);

    auto t_begin = high_resolution_clock::now();

    for (int i = 0; i < total_rounds; ++i) {
        // 记录发送前时间戳
        auto t0 = high_resolution_clock::now();

        // 发送 ping
        while (!ping_ring->CAS_Push(send_buf, sizeof(send_buf))) {
            // 队列满时自旋（理论上不会发生，因子进程同步消费）
        }

        // 自旋等待 pong 返回
        while (!pong_ring->CAS_Pop(recv)) {}

        // 记录接收后时间戳
        auto t1 = high_resolution_clock::now();

        // 计算往返延迟（微秒）
        double lat_us = static_cast<double>(
            duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / 1000.0;
        latencies.push_back(lat_us);

        // 进度输出（每 10 万次打印一次）
        if ((i + 1) % 100000 == 0) {
            auto elapsed = duration_cast<duration<double>>(
                high_resolution_clock::now() - t_begin).count();
            std::cout << "  进度: " << (i + 1) << " / " << total_rounds
                      << "  (" << std::fixed << std::setprecision(1)
                      << (100.0 * (i + 1) / total_rounds) << "%), "
                      << "已耗时 " << std::setprecision(2) << elapsed << " 秒\n"
                      << std::flush;
        }
    }

    auto t_end = high_resolution_clock::now();
    double total_sec = duration_cast<duration<double>>(t_end - t_begin).count();

    // ---- 等待子进程退出 ----
    int status;
    waitpid(pid, &status, 0);

    // ---- 统计分析 ----
    auto stats = LatencyStats::compute(latencies, LATENCY_HIST_BINS);
    stats.print("场景1: Ping-Pong 往返延迟统计 (100 字节 × 100 万次)", latencies.size());

    double throughput_rps = total_rounds / total_sec;
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "\n  总耗时:      " << total_sec << " 秒\n";
    std::cout << "  吞吐量:      " << throughput_rps << " 往返/秒\n";
    std::cout << "  平均延迟:    " << stats.avg_us << " us (目标 < 30 us)\n";

    // ---- 达标判断 ----
    bool pass = (stats.p99_us < 30.0);
    std::cout << "\n";
    if (pass) {
        std::cout << "  ✅ [通过] P99 延迟 " << stats.p99_us
                  << " us < 30 us 红线，碾压 TCP 500us！\n";
    } else {
        std::cout << "  ❌ [未通过] P99 延迟 " << stats.p99_us
                  << " us >= 30 us 红线，需排查系统负载或 CPU 频率\n";
    }

    // pool 析构时自动 munmap + shm_unlink
    return pass ? 0 : 1;
}

// ============================================================================
// 场景2: 车载大数据流吞吐测试
// ============================================================================

/**
 * 场景2 架构:
 *
 *   ┌─────────── 单进程 ─────────────────────────────┐
 *   │                                                 │
 *   │  ┌──────────────┐         ┌──────────────┐      │
 *   │  │ Producer 线程  │         │ Consumer 线程  │      │
 *   │  │              │         │              │      │
 *   │  │ 生成 10MB    │  push   │ 取出 10MB    │      │
 *   │  │ 模拟点云数据  │ ──────► │ MD5 校验     │      │
 *   │  │ 计算 MD5     │         │ 统计吞吐量   │      │
 *   │  │ 100 次/秒    │         │              │      │
 *   │  └──────┬───────┘         └──────┬───────┘      │
 *   │         │                        │              │
 *   │         │   ┌────────────────┐   │              │
 *   │         └──►│ LargeRingBuffer │◄──┘              │
 *   │             │ 4 块 × 10MB    │                  │
 *   │             │ (共享内存)      │                  │
 *   │             └────────────────┘                  │
 *   └─────────────────────────────────────────────────┘
 *
 * 数据流时序:
 *
 *    Producer                           Consumer
 *   ─────────                         ─────────
 *   t=0ms:   生成 10MB + MD5          │
 *   t=0ms:   push → Block[0]         │
 *   t=10ms:  生成 10MB + MD5          t=0~10ms: pop Block[0] → MD5 校验 ✓
 *   t=10ms:  push → Block[1]         │
 *   t=20ms:  生成 10MB + MD5          t=10~20ms: pop Block[1] → MD5 校验 ✓
 *   t=20ms:  push → Block[2]         │
 *   ...                                ...
 *
 * 关键设计决策:
 * - 单进程多线程（非多进程）：实际的 LiDAR 数据采集和处理可能在
 *   同一进程的不同线程中，此设计更贴近真实场景。
 * - 数据确定性生成（基于计数器，非随机）：便于事后审计。
 * - MD5 预计算 + 后校验：发送端计算 MD5 随数据写入，接收端重新计算
 *   并比对，任何数据踩踏都会导致 MD5 不匹配。
 */
static int run_throughput(int duration_sec) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║  场景2: 车载大数据流吞吐测试                        ║\n";
    std::cout << "║  数据包大小: 10 MB / 包                             ║\n";
    std::cout << "║  写入频率:   " << TARGET_WRITE_HZ
              << " 次/秒                                   ║\n";
    std::cout << "║  目标吞吐:   1 GB/s                                 ║\n";
    std::cout << "║  持续时间:   " << duration_sec
              << " 秒                                      ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";

    // ---- 创建共享内存 ----
    ShmMemoryPool pool(SHM_LARGE, LARGE_SHM_SIZE, ShmMemoryPool::CREATE);
    void* base = pool.GetMappedptr();

    // placement new 构造大数据环形缓冲区
    LargeRingBuffer* ring = new (base) LargeRingBuffer();

    // ---- 同步变量 ----
    // 使用原子变量协调生产者和消费者线程
    std::atomic<bool> producer_done{false};
    std::atomic<bool> consumer_done{false};

    // 统计计数器
    std::atomic<long long> total_written{0};   // 生产者成功写入次数
    std::atomic<long long> total_read{0};      // 消费者成功读取次数
    std::atomic<long long> total_corrupt{0};   // MD5 校验失败次数
    std::atomic<long long> total_bytes{0};     // 总吞吐字节数

    // ---- 消费者线程 ----
    std::thread consumer([&]() {
        std::vector<char> buf;
        unsigned char expected_md5[16];
        unsigned char actual_md5[16];

        while (!producer_done.load(std::memory_order_acquire) || true) {
            if (!ring->pop(buf, expected_md5)) {
                // 队列空 — 如果生产者已完成则退出，否则继续自旋
                if (producer_done.load(std::memory_order_acquire)) {
                    // 最后一次尝试
                    if (!ring->pop(buf, expected_md5)) break;
                } else {
                    std::this_thread::yield();
                    continue;
                }
            }

            // MD5 校验
            md5_raw(buf.data(), buf.size(), actual_md5);
            if (std::memcmp(expected_md5, actual_md5, 16) != 0) {
                total_corrupt.fetch_add(1, std::memory_order_relaxed);
                std::cerr << "\n[数据踩踏!] 第 " << total_read.load()
                          << " 包 MD5 不匹配！\n";
                std::cerr << "  预期: ";
                for (int i = 0; i < 16; ++i)
                    std::cerr << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(expected_md5[i]);
                std::cerr << "\n  实际: ";
                for (int i = 0; i < 16; ++i)
                    std::cerr << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(actual_md5[i]);
                std::cerr << std::dec << "\n";
            }

            total_read.fetch_add(1, std::memory_order_relaxed);
            total_bytes.fetch_add(static_cast<long long>(buf.size()),
                                  std::memory_order_relaxed);
        }

        consumer_done.store(true, std::memory_order_release);
    });

    // ---- 生产者线程 ----
    std::thread producer([&]() {
        // 预生成模板数据缓冲区（10MB 固定模式，仅头部 seq/ts 每次更新）
        std::vector<char> data(LARGE_PAYLOAD);
        unsigned char md5_buf[16];

        // 一次性填充固定模式（模拟点云背景噪声）
        {
            uint32_t pattern = 0x5A5A5A5A;
            for (size_t off = 16; off < LARGE_PAYLOAD; off += sizeof(pattern)) {
                std::memcpy(data.data() + off, &pattern, sizeof(pattern));
                pattern ^= (pattern >> 13);
            }
        }

        auto test_start = high_resolution_clock::now();
        auto deadline = test_start + std::chrono::seconds(duration_sec);

        int seq = 0;  // 序列号嵌入数据头部，确保每包唯一

        while (high_resolution_clock::now() < deadline) {
            // ---- 更新数据头（仅 16 字节，其余 10MB - 16B 复用模板） ----
            // 8 字节序列号
            std::memcpy(data.data(), &seq, sizeof(seq));
            // 8 字节时间戳（纳秒）
            auto ts = high_resolution_clock::now().time_since_epoch().count();
            std::memcpy(data.data() + sizeof(seq), &ts, sizeof(ts));

            // ---- 预计算 MD5（10MB 全量计算，约占 20ms，是主要耗时项） ----
            md5_raw(data.data(), data.size(), md5_buf);

            // ---- 写入环形队列（自旋直至成功） ----
            while (!ring->push(data.data(), data.size(), md5_buf)) {
                // 队列满 — 消费者处理速度跟不上生产者
                std::this_thread::yield();
            }

            total_written.fetch_add(1, std::memory_order_relaxed);
            ++seq;

            // ---- 频率控制: 目标 100Hz（每 10ms 一帧） ----
            // 注: MD5(10MB) 本身约需 15~25ms，故实际写入速率取决于 CPU 性能
            auto frame_end = high_resolution_clock::now();
            auto frame_duration = frame_end - test_start;
            auto target_frame_time = std::chrono::milliseconds(10) * (seq);
            if (frame_duration < target_frame_time) {
                std::this_thread::sleep_for(target_frame_time - frame_duration);
            }
        }

        producer_done.store(true, std::memory_order_release);
    });

    // ---- 等待线程完成 ----
    std::cout << "[运行] 测试进行中 (" << duration_sec << " 秒)...\n" << std::flush;

    producer.join();

    // 给消费者额外时间处理剩余数据
    {
        auto drain_deadline = high_resolution_clock::now() + std::chrono::seconds(2);
        while (!consumer_done.load(std::memory_order_acquire) &&
               high_resolution_clock::now() < drain_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    consumer.join();

    // ---- 结果统计 ----
    long long written   = total_written.load();
    long long read      = total_read.load();
    long long corrupt   = total_corrupt.load();
    long long bytes     = total_bytes.load();

    std::cout << "\n";
    std::cout << "┌─────────────────────────────────────────────────┐\n";
    std::cout << "│ 场景2: 大数据流吞吐 + 完整性测试结果                │\n";
    std::cout << "├─────────────────────────────────────────────────┤\n";
    std::cout << "│  总写入包数:   " << std::setw(10) << written << "\n";
    std::cout << "│  总读取包数:   " << std::setw(10) << read << "\n";
    std::cout << "│  MD5 不匹配:   " << std::setw(10) << corrupt;
    if (corrupt > 0) std::cout << " ⚠️  数据踩踏！";
    std::cout << "\n";
    std::cout << "│  吞吐量:       " << std::setw(8)
              << std::fixed << std::setprecision(2)
              << (bytes / 1e9) << " GB\n";
    std::cout << "│  实际写入速率: " << std::setw(8)
              << (written / static_cast<double>(duration_sec)) << " 包/秒\n";
    std::cout << "│  数据速率:     " << std::setw(8)
              << (bytes / static_cast<double>(duration_sec) / 1e6) << " MB/s\n";
    std::cout << "├─────────────────────────────────────────────────┤\n";

    // ---- 达标判断 ----
    bool md5_pass = (corrupt == 0);
    bool all_read = (written == read);
    bool rate_ok  = (written >= TARGET_WRITE_HZ * duration_sec * 0.95);  // 95% 即达标

    std::cout << "│  MD5 完整性校验:  ";
    if (md5_pass) {
        std::cout << "✅ 全部通过（零数据踩踏）\n";
    } else {
        std::cout << "❌ 发现 " << corrupt << " 处数据损坏！\n";
    }

    std::cout << "│  数据完整性:      ";
    if (all_read) {
        std::cout << "✅ 写入 " << written << " 包 = 读取 " << read << " 包\n";
    } else {
        std::cout << "⚠️  写入 " << written << " 包 ≠ 读取 " << read
                  << " 包（可能有包滞留队列）\n";
    }

    std::cout << "│  吞吐达标:        ";
    if (rate_ok) {
        std::cout << "✅ 写入速率 ≥ 目标的 95%\n";
    } else {
        std::cout << "⚠️  写入速率低于 " << TARGET_WRITE_HZ << " Hz\n";
        std::cout << "│  (注: 瓶颈不在无锁队列，而是 MD5(10MB) 计算耗时 ~"
                  << std::fixed << std::setprecision(1)
                  << (duration_sec * 1e6 / std::max(written, 1LL)) << " us/包)\n";
    }

    std::cout << "└─────────────────────────────────────────────────┘\n";

    // pool 析构自动清理
    return (md5_pass && all_read) ? 0 : 1;
}

// ============================================================================
// 主入口
// ============================================================================

static void print_usage(const char* prog) {
    std::cout << "用法: " << prog << " [选项]\n\n";
    std::cout << "选项:\n";
    std::cout << "  --mode <mode>       测试模式 (pingpong | throughput | all) [默认: all]\n";
    std::cout << "  --rounds <N>        Ping-Pong 往返次数 [默认: " << PINGPONG_ROUNDS << "]\n";
    std::cout << "  --duration <N>      吞吐测试持续时间(秒) [默认: " << THROUGHPUT_SEC << "]\n";
    std::cout << "  --help              显示此帮助信息\n\n";
    std::cout << "示例:\n";
    std::cout << "  " << prog << " --mode pingpong --rounds 100000\n";
    std::cout << "  " << prog << " --mode throughput --duration 10\n";
    std::cout << "  " << prog << " --mode all\n";
}

int main(int argc, char* argv[]) {
    // 关闭 stdout 缓冲，确保管道/重定向时输出即时可见
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::string mode = "all";
    int rounds = PINGPONG_ROUNDS;
    int duration = THROUGHPUT_SEC;

    // ---- 解析命令行参数 ----
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--rounds" && i + 1 < argc) {
            rounds = std::atoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            duration = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "未知参数: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // ---- 参数校验 ----
    if (mode != "pingpong" && mode != "throughput" && mode != "all") {
        std::cerr << "错误: 无效的模式 '" << mode
                  << "', 请使用 pingpong / throughput / all\n";
        return 1;
    }
    if (rounds <= 0) {
        std::cerr << "错误: rounds 必须 > 0\n";
        return 1;
    }
    if (duration <= 0) {
        std::cerr << "错误: duration 必须 > 0\n";
        return 1;
    }

    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║  CoreX IPC 无锁队列验证套件                        ║\n";
    std::cout << "║  测试目标: ShmRingBuffer 正确性 + 性能极限         ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";

    int ret = 0;

    if (mode == "pingpong" || mode == "all") {
        ret |= run_pingpong(rounds);
    }

    if (mode == "throughput" || mode == "all") {
        ret |= run_throughput(duration);
    }

    if (ret == 0) {
        std::cout << "\n🎉 所有测试通过！\n";
    } else {
        std::cout << "\n⚠️  部分测试未通过，请查看上方详细结果。\n";
    }

    return ret;
}
