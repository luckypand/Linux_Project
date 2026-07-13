#pragma once
// ============================================================================
// ServiceBridge.hpp — ROS Service ↔ CoreX RPC 映射适配器
//
// ROS Service 是同步 Request/Response，天然映射为 RPC：
//   CoreX RPC → ServiceBridge → ros::service::call() → 返回 response bytes
//
// 采用通用 bytes 透传方案：
//   payload = 序列化的 ROS Service Request bytes
//   response = 序列化的 ROS Service Response bytes
//   不依赖编译期已知的 .srv 类型
// ============================================================================

#include "BridgeConfig.hpp"
#include <ros/ros.h>
#include <string>
#include <memory>
#include <atomic>

class DynamicServiceAdapter;

class ServiceBridge
{
public:
    explicit ServiceBridge(const ServiceMappingConfig& cfg);
    ~ServiceBridge();

    ServiceBridge(const ServiceBridge&) = delete;
    ServiceBridge& operator=(const ServiceBridge&) = delete;

    bool start();
    void stop();

    DynamicServiceAdapter* getServiceAdapter() { return adapter_.get(); }

private:
    // RPC handler：接收请求 bytes，调用 ROS Service，返回响应 bytes
    std::string handleServiceCall(const std::string& payload);

    ServiceMappingConfig config_;
    std::unique_ptr<DynamicServiceAdapter> adapter_;
    ros::ServiceClient client_;
    std::atomic<bool> started_{false};
};
