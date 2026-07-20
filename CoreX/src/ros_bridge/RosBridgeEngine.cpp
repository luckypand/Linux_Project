// ============================================================================
// RosBridgeEngine.cpp — CoreX-ROS Bridge 核心引擎 实现
// ============================================================================

#include "RosBridgeEngine.hpp"
#include "RosNodeManager.hpp"
#include "TopicBridge.hpp"
#include "ServiceBridge.hpp"
#include "ActionBridge.hpp"
#include "GenericActionBridge.hpp"
#include "DynamicServiceAdapter.hpp"

#include "../rpc/RpcServiceAdapter.hpp"
#include <ros/ros.h>
#include <algorithm>

RosBridgeEngine::RosBridgeEngine() = default;
RosBridgeEngine::~RosBridgeEngine()
{
    stop();
}

// ============================================================================
// 初始化
// ============================================================================
bool RosBridgeEngine::initialize(const BridgeConfig& config)
{
    if (initialized_.load()) {
        ROS_WARN("[RosBridgeEngine] Already initialized");
        return true;
    }

    config_ = config;

    // 校验配置
    std::string errMsg;
    if (!config_.validate(errMsg)) {
        ROS_ERROR("[RosBridgeEngine] Config validation failed: %s", errMsg.c_str());
        return false;
    }

    if (!config_.enabled) {
        ROS_INFO("[RosBridgeEngine] Bridge is disabled in config, skipping");
        initialized_.store(true);
        return true;
    }

    // 初始化 ROS 节点
    auto& nodeMgr = RosNodeManager::instance();

    // 构造 argc/argv 用于 ros::init
    int    argc = 1;
    char   arg0[256];
    snprintf(arg0, sizeof(arg0), "%s", config_.nodeName.c_str());
    char*  argv[] = {arg0, nullptr};

    if (!nodeMgr.init(argc, argv, config_.nodeName, config_.spinnerThreads)) {
        ROS_ERROR("[RosBridgeEngine] Failed to initialize ROS node");
        return false;
    }

    // 等待 Master 连接（最多 3 秒）
    ros::Duration(0.5).sleep();
    if (!nodeMgr.isMasterConnected()) {
        ROS_WARN("[RosBridgeEngine] ROS Master not reachable — bridge will retry");
        // 不致命：Master 可能稍后启动
    }

    // 创建适配器
    createAdapters();

    initialized_.store(true);

    ROS_INFO("[RosBridgeEngine] Initialized: %zu topics, %zu services, %zu actions",
             topicBridges_.size(), serviceBridges_.size(), actionBridges_.size());

    return true;
}

// ============================================================================
// 创建适配器
// ============================================================================
void RosBridgeEngine::createAdapters()
{
    serviceAdapters_.clear();

    // ---- ★ 初始化 MessageTypeRegistry（从 YAML 加载 + 编译期回退）----
    typeRegistry_.loadFromConfig(config_);

    // ★ 设置全局 TypeRegistry（供 TopicBridge 使用）
    TopicBridge::setGlobalTypeRegistry(&typeRegistry_);

    // ---- 创建 TopicBridge 实例 ----
    for (const auto& cfg : config_.topics) {
        auto bridge = std::make_unique<TopicBridge>(cfg);
        topicBridges_.push_back(std::move(bridge));
    }

    // ---- 创建 ServiceBridge 实例 ----
    for (const auto& cfg : config_.services) {
        auto bridge = std::make_unique<ServiceBridge>(cfg);
        serviceBridges_.push_back(std::move(bridge));
    }

    // ---- 创建 ActionBridge 实例 ----
    for (const auto& cfg : config_.actions) {
        auto bridge = std::make_unique<ActionBridge>(cfg);
        actionBridges_.push_back(std::move(bridge));
    }

    // ---- ★ 创建 GenericActionBridge 实例 ----
    if (!config_.genericActions.empty()) {
        genericActionBridge_ = std::make_unique<GenericActionBridge>(
            config_, typeRegistry_);
    }

    // ---- 收集所有 RpcServiceAdapter 指针 ----
    for (auto& b : topicBridges_) {
        auto* adapter = b->getServiceAdapter();
        if (adapter) serviceAdapters_.push_back(adapter);
    }
    for (auto& b : serviceBridges_) {
        auto* adapter = b->getServiceAdapter();
        if (adapter) serviceAdapters_.push_back(adapter);
    }
    for (auto& b : actionBridges_) {
        auto* adapter = b->getServiceAdapter();
        if (adapter) serviceAdapters_.push_back(adapter);
    }
    // ★ 注册 GenericActionBridge adapter
    if (genericActionBridge_) {
        auto* adapter = genericActionBridge_->getServiceAdapter();
        if (adapter) serviceAdapters_.push_back(adapter);
    }
}

// ============================================================================
// 启动
// ============================================================================
bool RosBridgeEngine::start()
{
    if (running_.load()) return true;
    if (!initialized_.load()) {
        ROS_ERROR("[RosBridgeEngine] Must call initialize() before start()");
        return false;
    }
    if (!config_.enabled) {
        running_.store(true);
        return true;
    }

    ROS_INFO("[RosBridgeEngine] Starting %zu topic bridge(s)...", topicBridges_.size());

    for (auto& b : topicBridges_) {
        if (!b->start()) {
            ROS_ERROR("[RosBridgeEngine] Failed to start topic bridge");
            // 继续尝试启动其他的
        }
    }

    for (auto& b : serviceBridges_) {
        if (!b->start()) {
            ROS_ERROR("[RosBridgeEngine] Failed to start service bridge");
        }
    }

    for (auto& b : actionBridges_) {
        if (!b->start()) {
            ROS_ERROR("[RosBridgeEngine] Failed to start action bridge");
        }
    }

    // ★ 启动 GenericActionBridge
    if (genericActionBridge_) {
        if (!genericActionBridge_->start()) {
            ROS_ERROR("[RosBridgeEngine] Failed to start generic action bridge");
        }
    }

    running_.store(true);

    ROS_INFO("[RosBridgeEngine] Started: %zu service adapter(s) registered",
             serviceAdapters_.size());

    return true;
}

// ============================================================================
// 停止
// ============================================================================
void RosBridgeEngine::stop()
{
    if (!running_.load()) return;

    ROS_INFO("[RosBridgeEngine] Stopping all bridges...");

    // ★ 先停 GenericActionBridge
    if (genericActionBridge_) { genericActionBridge_->stop(); }

    // 按顺序停止：先 Action（需要清理 Goal），再 Service，最后 Topic
    for (auto& b : actionBridges_)   { b->stop(); }
    for (auto& b : serviceBridges_)  { b->stop(); }
    for (auto& b : topicBridges_)    { b->stop(); }

    // 清理适配器引用
    serviceAdapters_.clear();
    genericActionBridge_.reset();
    topicBridges_.clear();
    serviceBridges_.clear();
    actionBridges_.clear();

    // 关闭 ROS 节点
    RosNodeManager::instance().shutdown();

    initialized_.store(false);
    running_.store(false);

    ROS_INFO("[RosBridgeEngine] All bridges stopped");
}

// ============================================================================
// 查询接口
// ============================================================================
std::vector<RpcServiceAdapter*> RosBridgeEngine::getServiceAdapters() const
{
    return serviceAdapters_;
}

std::vector<std::string> RosBridgeEngine::getServiceNames() const
{
    std::vector<std::string> names;
    for (auto* adapter : serviceAdapters_) {
        if (adapter) {
            names.push_back(adapter->serviceName());
        }
    }
    return names;
}

BridgeHealthStatus RosBridgeEngine::healthCheck() const
{
    BridgeHealthStatus status;
    status.isRunning         = running_.load();
    status.topicBridgeCount  = static_cast<int>(topicBridges_.size());
    status.serviceBridgeCount = static_cast<int>(serviceBridges_.size());
    status.actionBridgeCount = static_cast<int>(actionBridges_.size());

    if (initialized_.load()) {
        status.masterConnected = RosNodeManager::instance().isMasterConnected();
    }

    // 统计活跃的 subscribe topic 数
    for (auto& b : topicBridges_) {
        if (b->hasData()) {
            status.activeTopics++;
        }
    }

    // 统计活跃的 Action 数
    for (auto& b : actionBridges_) {
        status.activeActions += b->activeGoalCount();
    }

    return status;
}
