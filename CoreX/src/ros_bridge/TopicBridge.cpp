// ============================================================================
// TopicBridge.cpp — ROS Topic ↔ CoreX RPC 双向映射 实现
// ============================================================================

#include "TopicBridge.hpp"
#include "DynamicServiceAdapter.hpp"
#include "RosNodeManager.hpp"
#include "ShmTopicBus.hpp"
#include "RosSerializedMessageTraits.hpp"
#include "MessageTypeRegistry.hpp"
#include <ros/ros.h>

// ============================================================================
// 全局 MessageTypeRegistry（由 RosBridgeEngine 设置）
// ============================================================================
static const MessageTypeRegistry* g_typeRegistry = nullptr;

void TopicBridge::setGlobalTypeRegistry(const MessageTypeRegistry* registry)
{
    g_typeRegistry = registry;
}

// ============================================================================
// 构造函数：创建 DynamicServiceAdapter 并注册 RPC 方法
// ============================================================================
TopicBridge::TopicBridge(const TopicMappingConfig& cfg)
    : config_(cfg)
    , useShmTopic_(cfg.useShmTopic)
{
    // 为每个 TopicBridge 创建一个独立的 DynamicServiceAdapter
    // service 名称使用配置中的 rpc_service
    adapter_ = std::make_unique<DynamicServiceAdapter>(cfg.rpcService);

    if (cfg.direction == TopicMappingConfig::Direction::Subscribe) {
        // Subscribe 方向：注册返回缓存数据的 handler
        adapter_->registerMethod(cfg.rpcMethod,
            [this](const std::string& payload) -> std::string {
                return this->handleGetCached(payload);
            });
    } else {
        // Publish 方向：注册发布到 ROS topic 的 handler
        adapter_->registerMethod(cfg.rpcMethod,
            [this](const std::string& payload) -> std::string {
                return this->handlePublish(payload);
            });
    }
}

TopicBridge::~TopicBridge()
{
    stop();
}

// ============================================================================
// 启动
// ============================================================================
bool TopicBridge::start()
{
    if (started_.load()) return true;

    if (config_.direction == TopicMappingConfig::Direction::Subscribe) {
        setupSubscriber();
    } else {
        setupPublisher();
    }

    // ★ 初始化 SHM Topic 直通通道
    if (useShmTopic_) {
        // 单消息最大大小：小消息 64KB, 图像类消息 10MB
        size_t maxMsgSize = (config_.useShm) ? (10 * 1024 * 1024) : (64 * 1024);
        bool isCreator = (config_.direction == TopicMappingConfig::Direction::Subscribe);
        // Subscribe 方向：Bridge 收到 ROS 消息后推入 SHM，是创建者
        // Publish 方向：Bridge 作为中转，也是创建者
        shmBus_ = std::make_unique<ShmTopicBus>(
            config_.rosTopic, maxMsgSize, 16, true);  // isCreator = true
        if (shmBus_->isValid()) {
            ROS_INFO("[TopicBridge] SHM topic channel enabled: %s msgSize=%zuB",
                     config_.rosTopic.c_str(), maxMsgSize);
        } else {
            ROS_WARN("[TopicBridge] SHM topic channel failed for %s, falling back to TCPROS only",
                     config_.rosTopic.c_str());
            shmBus_.reset();
            useShmTopic_ = false;
        }
    }

    started_.store(true);

    ROS_INFO("[TopicBridge] Started: %s [%s] direction=%s service=%s method=%s shm=%d",
             config_.rosTopic.c_str(), config_.rosType.c_str(),
             isSubscriber_ ? "ROS→CoreX(subscribe)" : "CoreX→ROS(publish)",
             config_.rpcService.c_str(), config_.rpcMethod.c_str(),
             useShmTopic_ ? 1 : 0);

    return true;
}

void TopicBridge::stop()
{
    if (!started_.load()) return;

    if (isSubscriber_) subscriber_.shutdown();
    if (isPublisher_)  publisher_.shutdown();

    isSubscriber_ = false;
    isPublisher_  = false;
    hasData_.store(false);

    started_.store(false);

    ROS_INFO("[TopicBridge] Stopped: %s", config_.rosTopic.c_str());
}

// ============================================================================
// Subscriber 设置 (ROS → CoreX)
// ============================================================================
void TopicBridge::setupSubscriber()
{
    auto& nodeMgr = RosNodeManager::instance();

    try {
        // 获取真实 MD5/Datatype（优先 YAML，回退编译期）
        std::string md5 = "*", datatype = config_.rosType;
        if (g_typeRegistry) {
            auto traits = g_typeRegistry->lookup(config_.rosType);
            md5 = traits.md5;
            datatype = traits.datatype;
        }

        // 使用通用 ros::SubscribeOptions 进行类型擦除的订阅
        // ros::SerializedMessage 可以接收任意 ROS 消息类型
        ros::SubscribeOptions ops;
        ops.topic      = config_.rosTopic;
        ops.queue_size = config_.queueSize;
        ops.datatype   = datatype;
        ops.md5sum     = md5;

        // 使用 boost 的通用订阅回调
        ops.helper = ros::SubscriptionCallbackHelperPtr(
            new ros::SubscriptionCallbackHelperT<
                const ros::SerializedMessage&>(
                    boost::bind(&TopicBridge::onRosMessage, this, _1)));

        subscriber_ = nodeMgr.nh().subscribe(ops);

        isSubscriber_ = true;

        ROS_INFO("[TopicBridge] Subscribed to %s [%s] queue=%d",
                 config_.rosTopic.c_str(), config_.rosType.c_str(),
                 config_.queueSize);

    } catch (const ros::Exception& e) {
        ROS_ERROR("[TopicBridge] Failed to subscribe to %s: %s",
                  config_.rosTopic.c_str(), e.what());
        isSubscriber_ = false;
    }
}

void TopicBridge::onRosMessage(const ros::SerializedMessage& msg)
{
    // 将序列化后的原始 ROS 消息存入缓存
    std::string data(reinterpret_cast<const char*>(msg.message_start),
                     msg.num_bytes);

    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cachedData_ = data;  // 保留一份给 RPC 查询
    }
    hasData_.store(true);

    // ★ 如果启用 SHM Topic 直通, 同步推入共享内存
    // 同机 CoreX 订阅者可以 < 10μs 延迟读取
    if (useShmTopic_ && shmBus_ && shmBus_->isValid()) {
        shmBus_->publish(msg.message_start, msg.num_bytes);
    }
}

// ============================================================================
// Publisher 设置 (CoreX → ROS)
// ============================================================================
void TopicBridge::setupPublisher()
{
    auto& nodeMgr = RosNodeManager::instance();

    try {
        // 获取真实 MD5/Datatype（优先 YAML，回退编译期）
        std::string md5 = "*", datatype = config_.rosType;
        if (g_typeRegistry) {
            auto traits = g_typeRegistry->lookup(config_.rosType);
            md5 = traits.md5;
            datatype = traits.datatype;
        }

        ros::AdvertiseOptions ops;
        ops.topic      = config_.rosTopic;
        ops.queue_size = config_.queueSize;
        ops.datatype   = datatype;
        ops.md5sum     = md5;

        publisher_ = nodeMgr.nh().advertise(ops);

        isPublisher_ = true;

        ROS_INFO("[TopicBridge] Publisher ready for %s [%s] queue=%d",
                 config_.rosTopic.c_str(), config_.rosType.c_str(),
                 config_.queueSize);

    } catch (const ros::Exception& e) {
        ROS_ERROR("[TopicBridge] Failed to create publisher for %s: %s",
                  config_.rosTopic.c_str(), e.what());
        isPublisher_ = false;
    }
}

// ============================================================================
// RPC Handler: Subscribe 方向 — 返回缓存数据
// ============================================================================
std::string TopicBridge::handleGetCached(const std::string& /*payload*/)
{
    std::lock_guard<std::mutex> lock(cacheMutex_);
    if (!hasData_.load()) return "";
    return cachedData_;  // 返回原始 ROS 序列化 bytes
}

// ============================================================================
// RPC Handler: Publish 方向 — 发布到 ROS topic (纯透传)
// 注意：格式转换层已移至 GenericActionBridge，此处仅做原始字节透传
// ============================================================================
std::string TopicBridge::handlePublish(const std::string& payload)
{
    if (!isPublisher_) {
        ROS_WARN("[TopicBridge] Not a publisher: %s", config_.rosTopic.c_str());
        return "";
    }

    try {
        // 原始字节透传
        ros::SerializedMessage serMsg;
        serMsg.num_bytes = payload.size();
        serMsg.buf.reset(new uint8_t[payload.size()]);
        serMsg.message_start = serMsg.buf.get();
        memcpy(serMsg.buf.get(), payload.data(), payload.size());

        // SHM 直通
        if (useShmTopic_ && shmBus_ && shmBus_->isValid()) {
            shmBus_->publish(payload.data(), payload.size());
        }

        publisher_.publish(serMsg);
        return "OK";

    } catch (const ros::Exception& e) {
        ROS_ERROR("[TopicBridge] Failed to publish to %s: %s",
                  config_.rosTopic.c_str(), e.what());
        return "";
    }
}
