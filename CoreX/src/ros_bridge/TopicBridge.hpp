#pragma once
// ============================================================================
// TopicBridge.hpp — ROS Topic ↔ CoreX RPC 双向映射适配器
//
// 支持两种方向：
//   Subscribe (ROS → CoreX)：订阅 ROS topic，缓存最新数据，暴露为 RPC 方法
//   Publish   (CoreX → ROS)：RPC 方法触发 → 发布 ROS topic
//
// 每个 TopicMappingConfig 对应一个 TopicBridge 实例。
// 使用 DynamicServiceAdapter 进行 RPC 方法注册，不依赖编译期 .proto。
// ============================================================================

#include "BridgeConfig.hpp"
#include <ros/ros.h>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>

class DynamicServiceAdapter;
class ShmTopicBus;
class MessageTypeRegistry;

class TopicBridge
{
public:
    // ★ 设置全局 MessageTypeRegistry（由 RosBridgeEngine 初始化时调用）
    static void setGlobalTypeRegistry(const MessageTypeRegistry* registry);

    explicit TopicBridge(const TopicMappingConfig& cfg);
    ~TopicBridge();

    // 禁止拷贝
    TopicBridge(const TopicBridge&) = delete;
    TopicBridge& operator=(const TopicBridge&) = delete;

    // ---- 生命周期 ----
    bool start();
    void stop();

    // ---- Service Adapter 获取 ----
    // 返回 DynamicServiceAdapter*，由 RosBridgeEngine 注册到 RpcServer
    DynamicServiceAdapter* getServiceAdapter() { return adapter_.get(); }

    // ---- 数据查询 ----
    bool hasData() const { return hasData_.load(); }

private:
    // ---- Subscribe 方向 ----
    void setupSubscriber();
    // 通用 ROS 消息订阅回调（使用 SerializedMessage）
    void onRosMessage(const ros::SerializedMessage& msg);

    // ---- Publish 方向 ----
    void setupPublisher();

    // ---- RPC Handler ----
    // Subscribe 方向的 handler：返回缓存的 ROS 消息 bytes
    std::string handleGetCached(const std::string& /*payload*/);
    // Publish 方向的 handler：接收 bytes → 发布到 ROS topic
    std::string handlePublish(const std::string& payload);

    TopicMappingConfig config_;
    std::unique_ptr<DynamicServiceAdapter> adapter_;

    // ROS 资源
    ros::Subscriber subscriber_;
    ros::Publisher  publisher_;
    bool isSubscriber_ = false;
    bool isPublisher_  = false;

    // 缓存 (subscribe 方向)
    mutable std::mutex cacheMutex_;
    std::string        cachedData_;   // 原始 ROS 序列化 bytes
    std::atomic<bool>  hasData_{false};

    // ★ SHM Topic 直通（同机节点高性能通道）
    std::unique_ptr<ShmTopicBus> shmBus_;
    bool useShmTopic_ = false;

    std::atomic<bool> started_{false};
};
