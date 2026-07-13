// ============================================================================
// test_ros_bridge.cpp — CoreX-ROS Bridge 单元测试
//
// 测试范围：
//   [NO ROS] BridgeConfig YAML 解析
//   [NO ROS] DynamicServiceAdapter 基本功能
//   [NO ROS] RosBridgeEngine 生命周期（无 ROS 模式）
//   [ROS]   TopicBridge 缓存读写线程安全（需 ROS 环境）
//   [ROS]   ShmImageTransporter 三缓冲正确性
//
// 编译：包含在 CMakeLists.txt 的 rosbridge_test target 中
// 运行：./build/test_ros_bridge
// ============================================================================

#include "../src/ros_bridge/BridgeConfig.hpp"
#include "../src/ros_bridge/DynamicServiceAdapter.hpp"
#include "../src/ros_bridge/RosCompat.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// 辅助宏
// ============================================================================
#define TEST(name)  printf("  [TEST] %s ... ", name)
#define PASS()      printf("PASS\n")
#define FAIL(msg)   do { printf("FAIL: %s\n", msg); return false; } while(0)
#define ASSERT(cond, msg) if (!(cond)) FAIL(msg)
#define ASSERT_EQ(a, b, msg) if ((a) != (b)) FAIL(msg)

static int g_passed = 0;
static int g_failed = 0;

static bool runTest(const char* name, bool (*fn)())
{
    printf("[TEST] %s ... ", name);
    if (fn()) {
        printf("PASS\n");
        g_passed++;
        return true;
    } else {
        g_failed++;
        return false;
    }
}

// ============================================================================
// 测试 1: BridgeConfig 基本解析
// ============================================================================
bool test_bridge_config_parse()
{
    // 构建 YAML 配置
    YAML::Node root;
    root["enabled"] = true;
    root["node_name"] = "test_bridge";
    root["spinner_threads"] = 4;

    // Topic 配置
    YAML::Node topics(YAML::NodeType::Sequence);
    YAML::Node t1;
    t1["ros_topic"] = "/cmd_vel";
    t1["ros_type"] = "geometry_msgs/Twist";
    t1["direction"] = "publish";
    t1["rpc_service"] = "MotionControl";
    t1["rpc_method"] = "SetVelocity";
    topics.push_back(t1);

    YAML::Node t2;
    t2["ros_topic"] = "/odom";
    t2["ros_type"] = "nav_msgs/Odometry";
    t2["direction"] = "subscribe";
    t2["rpc_service"] = "RobotTelemetry";
    t2["rpc_method"] = "GetOdometry";
    t2["rate_hz"] = 20;
    topics.push_back(t2);

    root["topics"] = topics;

    // Service 配置
    YAML::Node services(YAML::NodeType::Sequence);
    YAML::Node s1;
    s1["ros_service"] = "/gazebo/spawn_model";
    s1["rpc_service"] = "SimulationControl";
    s1["rpc_method"] = "SpawnModel";
    services.push_back(s1);
    root["services"] = services;

    // Action 配置
    YAML::Node actions(YAML::NodeType::Sequence);
    YAML::Node a1;
    a1["ros_action"] = "/move_base";
    a1["ros_action_type"] = "move_base_msgs/MoveBaseAction";
    a1["rpc_service"] = "Navigation";
    a1["rpc_method_start"] = "StartNav";
    actions.push_back(a1);
    root["actions"] = actions;

    // 解析
    BridgeConfig cfg;
    ASSERT(cfg.loadFromYaml(root), "loadFromYaml failed");
    ASSERT(cfg.enabled, "enabled should be true");
    ASSERT_EQ(cfg.nodeName, "test_bridge", "nodeName mismatch");
    ASSERT_EQ(cfg.spinnerThreads, 4, "spinnerThreads mismatch");

    // 验证 Topic 解析
    ASSERT_EQ(cfg.topics.size(), (size_t)2, "topics count mismatch");
    ASSERT_EQ(cfg.topics[0].rosTopic, "/cmd_vel", "topic[0] rosTopic mismatch");
    ASSERT_EQ(cfg.topics[0].rpcService, "MotionControl", "topic[0] svc mismatch");
    ASSERT(cfg.topics[0].direction == TopicMappingConfig::Direction::Publish,
           "topic[0] direction should be publish");
    ASSERT_EQ(cfg.topics[1].rosTopic, "/odom", "topic[1] rosTopic mismatch");
    ASSERT(cfg.topics[1].direction == TopicMappingConfig::Direction::Subscribe,
           "topic[1] direction should be subscribe");
    ASSERT_EQ(cfg.topics[1].rateHz, 20, "topic[1] rateHz mismatch");

    // 验证 Service 解析
    ASSERT_EQ(cfg.services.size(), (size_t)1, "services count mismatch");
    ASSERT_EQ(cfg.services[0].rosService, "/gazebo/spawn_model", "service mismatch");

    // 验证 Action 解析
    ASSERT_EQ(cfg.actions.size(), (size_t)1, "actions count mismatch");
    ASSERT_EQ(cfg.actions[0].rosAction, "/move_base", "action mismatch");
    ASSERT_EQ(cfg.actions[0].rpcMethodStart, "StartNav", "action start method");

    // 验证校验通过
    std::string err;
    ASSERT(cfg.validate(err), "validate should pass");

    return true;
}

// ============================================================================
// 测试 2: BridgeConfig 校验 — 缺失字段应失败
// ============================================================================
bool test_bridge_config_validation()
{
    // 空配置（disabled）应该通过
    BridgeConfig cfg1;
    cfg1.enabled = false;
    std::string err;
    ASSERT(cfg1.validate(err), "disabled config should pass");

    // 缺少必填字段应失败
    BridgeConfig cfg2;
    cfg2.enabled = true;
    TopicMappingConfig badTopic;
    badTopic.rosTopic = "/test";      // 有 topic 名
    // 但缺少 rpc_service 和 rpc_method
    cfg2.topics.push_back(badTopic);

    ASSERT(!cfg2.validate(err), "invalid config should fail");
    ASSERT(!err.empty(), "error message should not be empty");

    return true;
}

// ============================================================================
// 测试 3: DynamicServiceAdapter 基本功能
// ============================================================================
bool test_dynamic_service_adapter()
{
    DynamicServiceAdapter adapter("TestService");

    // 验证 serviceName
    ASSERT_EQ(adapter.serviceName(), "TestService", "serviceName mismatch");

    // 注册方法
    int callCount = 0;
    adapter.registerMethod("Echo",
        [&callCount](const std::string& payload) -> std::string {
            callCount++;
            return "echo: " + payload;
        });

    adapter.registerMethod("Add",
        [](const std::string& payload) -> std::string {
            return std::to_string(std::stoi(payload) * 2);
        });

    // 调用已注册的方法
    std::string r1 = adapter.dispatch("Echo", "hello");
    ASSERT_EQ(r1, "echo: hello", "Echo result mismatch");
    ASSERT_EQ(callCount, 1, "callCount should be 1");

    std::string r2 = adapter.dispatch("Add", "42");
    ASSERT_EQ(r2, "84", "Add result mismatch");

    // 调用未注册的方法应返回空
    std::string r3 = adapter.dispatch("NoSuchMethod", "data");
    ASSERT(r3.empty(), "unknown method should return empty");

    // 验证注册方法列表
    auto methods = adapter.registeredMethods();
    ASSERT_EQ(methods.size(), (size_t)2, "should have 2 methods");

    return true;
}

// ============================================================================
// 测试 4: DynamicServiceAdapter 并发安全
// ============================================================================
bool test_dynamic_service_adapter_concurrent()
{
    DynamicServiceAdapter adapter("ConcurrentTest");
    std::atomic<int> counter{0};

    adapter.registerMethod("Increment",
        [&counter](const std::string& /*p*/) -> std::string {
            counter.fetch_add(1);
            return "ok";
        });

    // 多线程并发调用
    const int numThreads = 8;
    const int callsPerThread = 1000;
    std::vector<std::thread> threads;

    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back([&adapter, callsPerThread]() {
            for (int j = 0; j < callsPerThread; j++) {
                std::string r = adapter.dispatch("Increment", "");
                if (r != "ok") {
                    printf("FAIL: unexpected response\n");
                    abort();
                }
            }
        });
    }

    for (auto& t : threads) t.join();

    ASSERT_EQ(counter.load(), numThreads * callsPerThread,
              "counter mismatch (concurrent safety issue)");

    return true;
}

// ============================================================================
// 测试 5: BridgeConfig 禁用模式
// ============================================================================
bool test_bridge_config_disabled()
{
    BridgeConfig cfg;
    // 默认 enabled = false
    ASSERT(!cfg.enabled, "default enabled should be false");

    std::string err;
    ASSERT(cfg.validate(err), "disabled empty config should pass");

    // 空 YAML 加载应成功（不是错误）
    YAML::Node emptyNode;
    ASSERT(cfg.loadFromYaml(emptyNode), "loading empty node should succeed");
    ASSERT(!cfg.enabled, "loading empty should keep disabled state");

    return true;
}

// ============================================================================
// 测试 6: YAML 解析边界情况
// ============================================================================
bool test_bridge_config_edge_cases()
{
    BridgeConfig cfg;

    // 空的 ros_bridge 段
    YAML::Node emptyRoot;
    emptyRoot["node_name"] = "empty_bridge";
    ASSERT(cfg.loadFromYaml(emptyRoot), "loading minimal config should succeed");

    // topics 不是 Sequence
    YAML::Node malformedRoot;
    malformedRoot["enabled"] = true;
    malformedRoot["topics"] = "not_a_sequence";  // 错误类型
    ASSERT(cfg.loadFromYaml(malformedRoot), "malformed topics should not crash");

    // 空的 topics 列表
    BridgeConfig cfg2;
    YAML::Node root2;
    root2["enabled"] = true;
    YAML::Node emptyTopics(YAML::NodeType::Sequence);
    root2["topics"] = emptyTopics;
    ASSERT(cfg2.loadFromYaml(root2), "empty topics list should succeed");
    ASSERT_EQ(cfg2.topics.size(), (size_t)0, "should have 0 topics");

    return true;
}

// ============================================================================
// ★ 测试 7-11: ShmTopicBus 共享内存 Topic 通信
// ============================================================================

#include "../src/ros_bridge/ShmTopicBus.hpp"
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/eventfd.h>
#include <poll.h>

// 测试 7: ShmTopicBus 基本读写
bool test_shmtopic_basic_rw()
{
    const char* topicName = "/test/basic_rw";
    size_t msgSize = 4096;

    // 清理可能的残留
    {
        ShmTopicBus cleaner(topicName, msgSize, 4, true);
    }

    // 创建者 (发布)
    ShmTopicBus pub(topicName, msgSize, 4, true);
    ASSERT(pub.isValid(), "publisher shm should be valid");

    // 订阅者
    ShmTopicBus sub(topicName, msgSize, 4, false);
    ASSERT(sub.isValid(), "subscriber shm should be valid");

    int efd = sub.subscribe();
    ASSERT(efd >= 0, "subscribe should return valid eventfd");

    // 发布一条消息
    std::string testMsg = "Hello SHM Topic Bus!";
    ASSERT(pub.publish(testMsg.data(), testMsg.size()), "publish should succeed");

    // 等待订阅者收到通知
    struct pollfd pfd;
    pfd.fd      = efd;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    int ret = ::poll(&pfd, 1, 1000);  // 1s timeout
    ASSERT(ret > 0, "subscriber should be notified via eventfd");
    ASSERT(pfd.revents & POLLIN, "eventfd should have POLLIN");

    // 读取消息
    std::string received;
    ASSERT(sub.tryRecv(received), "tryRecv should succeed");
    ASSERT_EQ(received, testMsg, "received message should match");

    // 没有新消息时 tryRecv 应返回 false
    ASSERT(!sub.tryRecv(received), "tryRecv should fail when no new message");

    close(efd);
    return true;
}

// 测试 8: ShmTopicBus 多订阅者并发
bool test_shmtopic_multi_subscriber()
{
    const char* topicName = "/test/multi_sub";
    size_t msgSize = 1024;
    const int numSubs = 3;

    {
        ShmTopicBus cleaner(topicName, msgSize, 8, true);
    }

    ShmTopicBus pub(topicName, msgSize, 8, true);
    ASSERT(pub.isValid(), "publisher valid");

    // 创建多个订阅者
    std::vector<std::unique_ptr<ShmTopicBus>> subs;
    std::vector<int> efds;
    for (int i = 0; i < numSubs; i++) {
        auto s = std::make_unique<ShmTopicBus>(topicName, msgSize, 8, false);
        ASSERT(s->isValid(), "subscriber valid");
        int fd = s->subscribe();
        ASSERT(fd >= 0, "subscribe ok");
        efds.push_back(fd);
        subs.push_back(std::move(s));
    }

    ASSERT_EQ(pub.subscriberCount(), numSubs, "subscriber count should match");

    // 发布消息
    std::string msg = "Broadcast message";
    ASSERT(pub.publish(msg.data(), msg.size()), "publish should succeed");

    // 验证所有订阅者都收到了
    for (int i = 0; i < numSubs; i++) {
        struct pollfd pfd;
        pfd.fd      = efds[i];
        pfd.events  = POLLIN;
        pfd.revents = 0;
        int ret = ::poll(&pfd, 1, 500);
        ASSERT(ret > 0, "subscriber should be notified");

        std::string received;
        ASSERT(subs[i]->tryRecv(received), "subscriber should receive");
        ASSERT_EQ(received, msg, "message should match");
    }

    for (int fd : efds) close(fd);
    return true;
}

// 测试 9: ShmTopicBus 环形覆盖（超过槽位数）
bool test_shmtopic_ring_overwrite()
{
    const char* topicName = "/test/ring_overwrite";
    size_t msgSize = 256;
    const int numSlots = 4;

    {
        ShmTopicBus cleaner(topicName, msgSize, numSlots, true);
    }

    ShmTopicBus pub(topicName, msgSize, numSlots, true);
    ShmTopicBus sub(topicName, msgSize, numSlots, false);

    int efd = sub.subscribe();
    ASSERT(efd >= 0, "subscribe ok");

    // 发布 10 条消息（超过 4 个槽位）
    for (int i = 0; i < 10; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "msg_%03d", i);
        ASSERT(pub.publish(buf, strlen(buf) + 1), "publish should succeed");
    }

    // 等待事件通知
    struct pollfd pfd;
    pfd.fd      = efd;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    ::poll(&pfd, 1, 500);

    // 读取 — 应该是最新消息 "msg_009"
    std::string received;
    ASSERT(sub.tryRecv(received), "should get latest message");
    ASSERT(received.find("msg_009") != std::string::npos, "should get latest, not oldest");

    // 验证写序号
    ASSERT_EQ(pub.lastWriteSeq(), (uint64_t)10, "write_seq should be 10");

    close(efd);
    return true;
}

// 测试 10: ShmTopicBus eventfd 通知机制
bool test_shmtopic_eventfd_wakeup()
{
    const char* topicName = "/test/eventfd_wake";
    size_t msgSize = 512;

    {
        ShmTopicBus cleaner(topicName, msgSize, 8, true);
    }

    ShmTopicBus pub(topicName, msgSize, 8, true);
    ShmTopicBus sub(topicName, msgSize, 8, false);

    int efd = sub.subscribe();
    ASSERT(efd >= 0, "subscribe ok");

    // 使用独立线程模拟阻塞等待
    std::atomic<bool> notified{false};
    std::atomic<bool> dataOk{false};

    std::thread waiter([&]() {
        struct pollfd pfd;
        pfd.fd      = efd;
        pfd.events  = POLLIN;
        pfd.revents = 0;
        int ret = ::poll(&pfd, 1, 2000);  // 2s timeout
        if (ret > 0 && (pfd.revents & POLLIN)) {
            notified.store(true);
            // 排空 eventfd
            uint64_t val;
            ::read(efd, &val, sizeof(val));

            std::string msg;
            if (sub.tryRecv(msg)) {
                dataOk.store(msg == "wakeup_test");
            }
        }
    });

    // 等待一小段时间确保 waiter 已进入 poll
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 发布 → 唤醒 waiter
    std::string msg = "wakeup_test";
    ASSERT(pub.publish(msg.data(), msg.size()), "publish succeed");

    waiter.join();

    ASSERT(notified.load(), "waiter should be woken by eventfd");
    ASSERT(dataOk.load(), "waiter should receive correct data");

    close(efd);
    return true;
}

// 测试 11: ShmTopicBus use_shm_topic 配置解析
bool test_shmtopic_config_parsing()
{
    YAML::Node topicYaml;
    topicYaml["ros_topic"] = "/camera/image_raw";
    topicYaml["ros_type"] = "sensor_msgs/Image";
    topicYaml["direction"] = "subscribe";
    topicYaml["rpc_service"] = "RobotTelemetry";
    topicYaml["rpc_method"] = "GetImage";
    topicYaml["use_shm_topic"] = true;
    topicYaml["queue_size"] = 5;

    // 通过 BridgeConfig 解析
    BridgeConfig cfg;
    YAML::Node root;
    root["enabled"] = true;
    YAML::Node topics(YAML::NodeType::Sequence);
    topics.push_back(topicYaml);
    root["topics"] = topics;

    ASSERT(cfg.loadFromYaml(root), "load config");
    ASSERT_EQ(cfg.topics.size(), (size_t)1, "1 topic");
    ASSERT(cfg.topics[0].useShmTopic, "use_shm_topic should be true");
    ASSERT_EQ(cfg.topics[0].rosTopic, "/camera/image_raw", "topic name");
    ASSERT_EQ(cfg.topics[0].queueSize, 5, "queue size");

    // 默认值：未设置 use_shm_topic 时应为 false
    BridgeConfig cfg2;
    YAML::Node root2;
    root2["enabled"] = true;
    YAML::Node topics2(YAML::NodeType::Sequence);
    YAML::Node t2;
    t2["ros_topic"] = "/odom";
    t2["ros_type"] = "nav_msgs/Odometry";
    t2["direction"] = "subscribe";
    t2["rpc_service"] = "Telemetry";
    t2["rpc_method"] = "GetOdometry";
    topics2.push_back(t2);
    root2["topics"] = topics2;

    ASSERT(cfg2.loadFromYaml(root2), "load config2");
    ASSERT(!cfg2.topics[0].useShmTopic, "use_shm_topic default should be false");

    return true;
}

// ============================================================================
// main
// ============================================================================
int main()
{
    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║   CoreX-ROS Bridge Unit Tests                ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    printf("--- Module tests (no ROS required) ---\n");

    runTest("BridgeConfig parse",              test_bridge_config_parse);
    runTest("BridgeConfig validation",          test_bridge_config_validation);
    runTest("BridgeConfig disabled mode",       test_bridge_config_disabled);
    runTest("BridgeConfig edge cases",          test_bridge_config_edge_cases);
    runTest("DynamicServiceAdapter basic",      test_dynamic_service_adapter);
    runTest("DynamicServiceAdapter concurrent", test_dynamic_service_adapter_concurrent);

    printf("\n--- ShmTopicBus tests (SHM pub/sub, no ROS required) ---\n");

    runTest("ShmTopicBus basic read/write",    test_shmtopic_basic_rw);
    runTest("ShmTopicBus multi-subscriber",     test_shmtopic_multi_subscriber);
    runTest("ShmTopicBus ring overwrite",       test_shmtopic_ring_overwrite);
    runTest("ShmTopicBus eventfd wakeup",       test_shmtopic_eventfd_wakeup);
    runTest("ShmTopicBus config parsing",       test_shmtopic_config_parsing);

    printf("\n--- Results ---\n");
    printf("  Passed: %d\n", g_passed);
    printf("  Failed: %d\n", g_failed);
    printf("  Total:  %d\n", g_passed + g_failed);

    if (g_failed > 0) {
        printf("\n*** SOME TESTS FAILED ***\n");
        return 1;
    }

    printf("\n*** ALL TESTS PASSED ***\n");
    return 0;
}
