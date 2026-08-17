// ============================================================
// test_pubsub.cpp — 共享内存 Pub/Sub 系统综合测试
// ============================================================
// 用法: ./test_pubsub --mode <mode>
//   spsc    — 单生产者单消费者基础测试
//   spmc    — 单生产者多消费者（ref_count 核心验证）
//   mpsc    — 多生产者单消费者
//   mpmc    — 多生产者多消费者
//   stream  — STREAM QoS 测试
//   keep_last — KEEP_LAST QoS 测试
//   latch   — LATCH QoS 测试
//   timeout — 心跳超时回收测试
//   all     — 运行全部测试

#include "TopicManager.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>

// ============================================================
// 测试辅助
// ============================================================

static const char* PS_NAME   = "/test_ps";
static const char* POOL_NAME = "/test_ps_pool";
static int g_pass = 0, g_fail = 0;

#define TASSERT(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); g_fail++; return; } \
    else { printf("  PASS: %s\n", msg); g_pass++; } \
} while(0)

void cleanup()
{
    shm_unlink(PS_NAME);
    shm_unlink(POOL_NAME);
}

uint64_t now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 创建标准的 TopicManager（64 blocks × 1MB）
TopicManager* createMgr(uint32_t blocks = 256)
{
    cleanup();
    // ring_cap=128：支持大批量压测不丢帧
    return new TopicManager(PS_NAME, POOL_NAME, 8, blocks, 1048576, 8, 128);
}

// ============================================================
// 测试用例
// ============================================================

void test_spsc()
{
    printf("\n--- SPSC: 单生产者单消费者 ---\n");
    auto mgr = createMgr();

    int32_t tid = mgr->createTopic("/test", {QoSType::STREAM});
    TASSERT(tid >= 0, "create topic");

    // 生产者发布 100 条
    const int N = 100;
    for (int i = 0; i < N; i++) {
        TASSERT(mgr->publish(tid, &i, sizeof(i)) != UINT32_MAX, "publish");
    }

    // 消费者接收（STREAM 无历史）
    int32_t sub = mgr->subscribe(tid);
    TASSERT(sub >= 0, "subscribe");

    // 生产者再发 100 条（消费者应收到）
    for (int i = 0; i < N; i++) {
        int val = i + 1000;
        mgr->publish(tid, &val, sizeof(val));
    }

    for (int i = 0; i < N; i++) {
        int val;
        uint32_t sz = sizeof(val);
        TASSERT(mgr->receive(tid, sub, &val, sz), "receive");
        TASSERT(val == i + 1000, "data correct");
    }

    // 无更多数据
    int dummy; uint32_t sz = sizeof(dummy);
    TASSERT(!mgr->receive(tid, sub, &dummy, sz), "no more data");

    delete mgr;
}

void test_spmc()
{
    printf("\n--- SPMC: 单生产者多消费者（ref_count 核心）---\n");
    auto mgr = createMgr();

    int32_t tid = mgr->createTopic("/spmc", {QoSType::STREAM});
    TASSERT(tid >= 0, "create");

    // 3 个订阅者
    int32_t s0 = mgr->subscribe(tid);
    int32_t s1 = mgr->subscribe(tid);
    int32_t s2 = mgr->subscribe(tid);
    TASSERT(s0 >= 0 && s1 >= 0 && s2 >= 0, "3 subscribers");

    // 发布 50 条，每个订阅者都应收到相同数据
    const int N = 50;
    for (int i = 0; i < N; i++) {
        TASSERT(mgr->publish(tid, &i, sizeof(i)) != UINT32_MAX, "publish");
    }

    for (int i = 0; i < N; i++) {
        int v0, v1, v2;
        uint32_t sz = sizeof(int);
        TASSERT(mgr->receive(tid, s0, &v0, sz) && v0 == i, "s0 recv");
        sz = sizeof(int);
        TASSERT(mgr->receive(tid, s1, &v1, sz) && v1 == i, "s1 recv");
        sz = sizeof(int);
        TASSERT(mgr->receive(tid, s2, &v2, sz) && v2 == i, "s2 recv");
    }

    delete mgr;
}

void test_mpsc()
{
    printf("\n--- MPSC: 多线程生产者单消费者 ---\n");
    auto mgr = createMgr(512);  // extra blocks for concurrent alloc

    int32_t tid = mgr->createTopic("/mpsc", {QoSType::STREAM});
    TASSERT(tid >= 0, "create");

    // ★ 先订阅，再启动生产者（确保生产者发布时已有消费者）
    int32_t sub = mgr->subscribe(tid);
    TASSERT(sub >= 0, "subscribe");

    const int PER = 30;  // 4*30=120 < ring_cap=128, 不触发覆盖
    std::vector<std::thread> producers;
    for (int p = 0; p < 4; p++) {
        producers.emplace_back([mgr, tid, p, PER]() {
            for (int i = 0; i < PER; i++) {
                int val = p * 1000 + i;
                mgr->publish(tid, &val, sizeof(val));
            }
        });
    }
    for (auto& t : producers) t.join();

    // 边收边验证，最多收 4*PER 条
    int count = 0;
    int val; uint32_t sz = sizeof(val);
    for (int i = 0; i < 4 * PER; i++) {
        if (mgr->receive(tid, sub, &val, sz)) count++;
    }
    printf("  MPSC received %d / %d msgs\n", count, 4 * PER);
    TASSERT(count == 4 * PER, "all msgs received");

    delete mgr;
}

void test_mpmc()
{
    printf("\n--- MPMC: 多生产者多消费者跨进程 ---\n");
    cleanup();

    // 使用大 ring_cap 避免覆盖
    TopicManager mgr(PS_NAME, POOL_NAME, 8, 512, 65536, 8, 128);
    int32_t tid = mgr.createTopic("/mpmc", {QoSType::STREAM});
    TASSERT(tid >= 0, "create");

    int32_t sub0 = mgr.subscribe(tid);
    TASSERT(sub0 >= 0, "parent subscribe");

    pid_t pid = fork();
    TASSERT(pid >= 0, "fork");

    if (pid == 0) {
        TopicManager child(PS_NAME, POOL_NAME);
        int32_t ctid = child.findTopic("/mpmc");
        assert(ctid >= 0);

        int32_t cs = child.subscribe(ctid);
        assert(cs >= 0);

        for (int i = 0; i < 30; i++)
            child.publish(ctid, &i, sizeof(i));

        // 确认子进程能收到自己的消息
        int count = 0, v; uint32_t sz = sizeof(v);
        for (int i = 0; i < 30; i++)
            if (child.receive(ctid, cs, &v, sz)) count++;
        printf("  child recv: %d\n", count);
        _exit(0);
    } else {
        // ★ 等子进程先订阅，然后发布（确保双方都是活跃订阅者）
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        for (int i = 100; i < 130; i++)
            mgr.publish(tid, &i, sizeof(i));

        waitpid(pid, nullptr, 0);

        int count = 0, v; uint32_t sz = sizeof(v);
        for (int i = 0; i < 60; i++)
            if (mgr.receive(tid, sub0, &v, sz)) count++;
        printf("  parent recv: %d\n", count);
        TASSERT(count == 60, "parent received all 60 msgs");
    }
}

void test_stream()
{
    printf("\n--- STREAM QoS: 无历史 ---\n");
    auto mgr = createMgr();

    int32_t tid = mgr->createTopic("/stream", {QoSType::STREAM});
    mgr->publish(tid, "old1", 5);
    mgr->publish(tid, "old2", 5);

    int32_t sub = mgr->subscribe(tid);
    TASSERT(sub >= 0, "subscribe");

    char b[32]; uint32_t sz = 32;
    TASSERT(!mgr->receive(tid, sub, b, sz), "no history for STREAM");

    mgr->publish(tid, "new", 4);
    TASSERT(mgr->receive(tid, sub, b, sz) && strcmp(b, "new") == 0, "receive future data");

    delete mgr;
}

void test_keep_last()
{
    printf("\n--- KEEP_LAST QoS: 历史重放 ---\n");
    auto mgr = createMgr();

    int32_t tid = mgr->createTopic("/kl", {QoSType::KEEP_LAST, 3});
    for (int i = 1; i <= 5; i++) {
        char m[16]; snprintf(m, 16, "m%d", i);
        mgr->publish(tid, m, strlen(m) + 1);
    }

    int32_t sub = mgr->subscribe(tid);
    TASSERT(sub >= 0, "subscribe");

    // 应收到最后 3 条: m3, m4, m5
    for (int i = 3; i <= 5; i++) {
        char b[16] = {}; uint32_t sz = 16;
        TASSERT(mgr->receive(tid, sub, b, sz), "receive history");
        char e[16]; snprintf(e, 16, "m%d", i);
        TASSERT(strcmp(b, e) == 0, "correct history order");
    }

    delete mgr;
}

void test_latch()
{
    printf("\n--- LATCH QoS: 锁存最新 ---\n");
    auto mgr = createMgr();

    int32_t tid = mgr->createTopic("/latch", {QoSType::LATCH});
    mgr->publish(tid, "v1", 3);
    mgr->publish(tid, "v2", 3);
    mgr->publish(tid, "v3_final", 9);

    // 多次订阅都应收到最新的 v3_final
    for (int i = 0; i < 3; i++) {
        int32_t sub = mgr->subscribe(tid);
        TASSERT(sub >= 0, "subscribe");

        char b[32] = {}; uint32_t sz = 32;
        TASSERT(mgr->receive(tid, sub, b, sz), "receive latched");
        TASSERT(strcmp(b, "v3_final") == 0, "latched value correct");
    }

    // 更新后新订阅者收到新值
    mgr->publish(tid, "v4", 3);
    int32_t s4 = mgr->subscribe(tid);
    char b4[32] = {}; uint32_t sz4 = 32;
    TASSERT(mgr->receive(tid, s4, b4, sz4) && strcmp(b4, "v4") == 0, "latch updated");

    delete mgr;
}

void test_timeout()
{
    printf("\n--- Heartbeat Timeout: 超时回收 ---\n");
    auto mgr = createMgr();

    int32_t tid = mgr->createTopic("/to", {QoSType::STREAM});
    int32_t sub = mgr->subscribe(tid);
    TASSERT(sub >= 0, "subscribe");

    // 发数据 + 心跳
    mgr->publish(tid, "data", 5);
    mgr->heartbeat(tid, sub, now_us());

    // 立即检查：不应超时
    mgr->recoverOnce(1000000);
    // sub 仍有效
    int32_t s2 = mgr->subscribe(tid);  // 新订阅者，验证 sub 仍活跃
    TASSERT(s2 >= 0 && s2 != sub, "old sub still active, new sub different");

    // 让心跳过期
    mgr->heartbeat(tid, sub, now_us() - 5000000);  // 5 秒前的心跳
    mgr->recoverOnce(1000000);  // 1 秒超时 → sub 应被回收

    // 回收后再发数据，原 sub 应收不到（已被注销）
    mgr->publish(tid, "nobody", 7);
    char b[32]; uint32_t sz = 32;
    TASSERT(!mgr->receive(tid, sub, b, sz), "timed-out subscriber cannot recv");

    delete mgr;
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[])
{
    std::string mode = (argc > 2 && strcmp(argv[1], "--mode") == 0) ? argv[2] : "all";

    printf("=== CoreX Pub/Sub Tests (mode=%s) ===\n", mode.c_str());

    if (mode == "spsc" || mode == "all")     test_spsc();
    if (mode == "spmc" || mode == "all")     test_spmc();
    if (mode == "mpsc" || mode == "all")     test_mpsc();
    if (mode == "mpmc" || mode == "all")     test_mpmc();
    if (mode == "stream" || mode == "all")   test_stream();
    if (mode == "keep_last" || mode == "all") test_keep_last();
    if (mode == "latch" || mode == "all")    test_latch();
    if (mode == "timeout" || mode == "all")  test_timeout();

    printf("\n=== Results: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    cleanup();
    return g_fail > 0 ? 1 : 0;
}
