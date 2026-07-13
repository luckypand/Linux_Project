// ============================================================================
// ServiceBridge.cpp — ROS Service ↔ CoreX RPC 映射 实现
// ============================================================================

#include "ServiceBridge.hpp"
#include "DynamicServiceAdapter.hpp"
#include "RosNodeManager.hpp"
#include <ros/ros.h>

ServiceBridge::ServiceBridge(const ServiceMappingConfig& cfg)
    : config_(cfg)
{
    adapter_ = std::make_unique<DynamicServiceAdapter>(cfg.rpcService);

    adapter_->registerMethod(cfg.rpcMethod,
        [this](const std::string& payload) -> std::string {
            return this->handleServiceCall(payload);
        });
}

ServiceBridge::~ServiceBridge()
{
    stop();
}

bool ServiceBridge::start()
{
    if (started_.load()) return true;

    auto& nodeMgr = RosNodeManager::instance();

    try {
        // 创建持久化的通用 ServiceClient
        client_ = nodeMgr.nh().serviceClient<ros::SerializedMessage>(
            config_.rosService, true);  // persistent = true

        started_.store(true);

        ROS_INFO("[ServiceBridge] Started: %s → %s.%s (timeout=%dms)",
                 config_.rosService.c_str(),
                 config_.rpcService.c_str(), config_.rpcMethod.c_str(),
                 config_.timeoutMs);

    } catch (const ros::Exception& e) {
        ROS_ERROR("[ServiceBridge] Failed to start: %s", e.what());
        return false;
    }

    return true;
}

void ServiceBridge::stop()
{
    if (!started_.load()) return;
    client_.shutdown();
    started_.store(false);
    ROS_INFO("[ServiceBridge] Stopped: %s", config_.rosService.c_str());
}

std::string ServiceBridge::handleServiceCall(const std::string& payload)
{
    // payload = 序列化的 ROS Service Request bytes
    ros::SerializedMessage srvReq;
    srvReq.num_bytes = payload.size();
    srvReq.buf.reset(new uint8_t[payload.size()]);
    srvReq.message_start = srvReq.buf.get();
    memcpy(srvReq.buf.get(), payload.data(), payload.size());

    ros::SerializedMessage srvResp;
    ros::Duration timeout(config_.timeoutMs / 1000.0);

    if (!client_.call(srvReq, srvResp, timeout)) {
        ROS_ERROR("[ServiceBridge] Call to '%s' failed (timeout=%dms)",
                  config_.rosService.c_str(), config_.timeoutMs);
        return "";  // 空串表示失败
    }

    // 返回序列化的 ROS Service Response bytes
    return std::string(
        reinterpret_cast<const char*>(srvResp.message_start),
        srvResp.num_bytes);
}
