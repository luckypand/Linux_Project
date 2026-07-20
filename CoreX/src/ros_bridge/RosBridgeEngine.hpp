#pragma once
// ============================================================================
// RosBridgeEngine.hpp — CoreX-ROS Bridge 核心引擎
//
// 职责：
//   1. 读取 BridgeConfig，创建对应的适配器实例
//   2. 管理所有适配器的生命周期（启动 / 停止）
//   3. 生成 RpcServiceAdapter 列表供 RpcServer 注册
//   4. 健康检查（ROS Master 连通性、适配器状态）
//
// 与 CoreXDaemon 的集成：
//   CoreXDaemon::init() 中解析 ros_bridge YAML → BridgeConfig
//   CoreXDaemon::start() 中调用 engine.start()
//   CoreXDaemon::shutdown() 中调用 engine.stop()
// ============================================================================

#include "BridgeConfig.hpp"
#include "MessageTypeRegistry.hpp"
#include <memory>
#include <vector>
#include <string>
#include <atomic>

// 前向声明
class RpcServiceAdapter;

// 内部适配器（不暴露到头文件外部）
class TopicBridge;
class ServiceBridge;
class ActionBridge;
class GenericActionBridge;

// ============================================================================
// 健康检查结果
// ============================================================================
struct BridgeHealthStatus
{
    bool   masterConnected = false;
    int    topicBridgeCount = 0;
    int    serviceBridgeCount = 0;
    int    actionBridgeCount = 0;
    int    activeTopics = 0;       // 实际有数据的 subscribe topic 数
    int    activeActions = 0;      // 正在执行中的 Action 数
    bool   isRunning = false;

    bool allOk() const {
        return masterConnected && isRunning;
    }
};

// ============================================================================
// RosBridgeEngine
// ============================================================================
class RosBridgeEngine
{
public:
    RosBridgeEngine();
    ~RosBridgeEngine();

    // 禁止拷贝 / 移动
    RosBridgeEngine(const RosBridgeEngine&) = delete;
    RosBridgeEngine& operator=(const RosBridgeEngine&) = delete;

    // ---- 生命周期 ----

    // 加载配置（在 start() 之前调用）
    // @return true 配置有效
    bool initialize(const BridgeConfig& config);

    // 启动所有适配器（创建 ROS 订阅者/发布者/服务客户端等）
    // @return true 全部启动成功
    bool start();

    // 停止所有适配器，释放 ROS 资源
    void stop();

    // ---- 查询接口 ----

    // 返回所有需要注册到 RpcServer 的 ServiceAdapter
    // 在 start() 之后调用
    std::vector<RpcServiceAdapter*> getServiceAdapters() const;

    // 健康检查
    BridgeHealthStatus healthCheck() const;

    // 是否已启动
    bool isRunning() const { return running_.load(); }

    // 获取注册的 service 名称列表（用于日志/调试打印）
    std::vector<std::string> getServiceNames() const;

    // 获取配置引用（只读）
    const BridgeConfig& config() const { return config_; }

private:
    // 内部：创建所有适配器实例
    void createAdapters();

    BridgeConfig config_;

    // 适配器实例（由 RosBridgeEngine 管理生命周期）
    std::vector<std::unique_ptr<TopicBridge>>   topicBridges_;
    std::vector<std::unique_ptr<ServiceBridge>> serviceBridges_;
    std::vector<std::unique_ptr<ActionBridge>>  actionBridges_;
    std::unique_ptr<GenericActionBridge>        genericActionBridge_; // ★

    // ★ 消息类型注册表（YAML 驱动 + 编译期回退）
    MessageTypeRegistry typeRegistry_;

    // 聚合后的 RpcServiceAdapter 指针（供 getServiceAdapters 返回）
    std::vector<RpcServiceAdapter*> serviceAdapters_;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
};
