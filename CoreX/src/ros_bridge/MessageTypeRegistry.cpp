// ============================================================================
// MessageTypeRegistry.cpp — YAML 驱动的 ROS 消息类型注册表 实现
// ============================================================================

#include "MessageTypeRegistry.hpp"
#include "BridgeConfig.hpp"

// 编译期已知类型（保留硬编码作为回退）
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/JointState.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/LaserScan.h>
#include <ros/ros.h>

// ============================================================================
// 从 BridgeConfig 加载
// ============================================================================
void MessageTypeRegistry::loadFromConfig(const BridgeConfig& config)
{
    yamlTypes_.clear();
    for (const auto& [typeName, info] : config.messageTypes) {
        yamlTypes_[typeName] = {info.md5, info.datatype};
    }
}

// ============================================================================
// 查询
// ============================================================================
MessageTypeRegistry::TypeTraits MessageTypeRegistry::lookup(
    const std::string& rosType) const
{
    // 1. YAML 配置优先
    auto it = yamlTypes_.find(rosType);
    if (it != yamlTypes_.end()) {
        return it->second;
    }

    // 2. 编译期回退
    auto fallback = compileTimeFallback(rosType);
    if (!fallback.md5.empty() && fallback.md5 != "*") {
        return fallback;
    }

    // 3. 保底
    return {"*", rosType};
}

// ============================================================================
// 手动注册
// ============================================================================
void MessageTypeRegistry::addType(const std::string& rosType,
                                   const std::string& md5,
                                   const std::string& datatype)
{
    yamlTypes_[rosType] = {md5, datatype};
}

// ============================================================================
// 编译期回退（从原 TopicBridge::getMessageTraits() 迁移）
// ============================================================================
MessageTypeRegistry::TypeTraits MessageTypeRegistry::compileTimeFallback(
    const std::string& rosType)
{
    if (rosType == "geometry_msgs/Twist") {
        return {ros::message_traits::md5sum<geometry_msgs::Twist>(),
                ros::message_traits::datatype<geometry_msgs::Twist>()};
    }
    if (rosType == "nav_msgs/Odometry") {
        return {ros::message_traits::md5sum<nav_msgs::Odometry>(),
                ros::message_traits::datatype<nav_msgs::Odometry>()};
    }
    if (rosType == "sensor_msgs/JointState") {
        return {ros::message_traits::md5sum<sensor_msgs::JointState>(),
                ros::message_traits::datatype<sensor_msgs::JointState>()};
    }
    if (rosType == "sensor_msgs/Image") {
        return {ros::message_traits::md5sum<sensor_msgs::Image>(),
                ros::message_traits::datatype<sensor_msgs::Image>()};
    }
    if (rosType == "sensor_msgs/LaserScan") {
        return {ros::message_traits::md5sum<sensor_msgs::LaserScan>(),
                ros::message_traits::datatype<sensor_msgs::LaserScan>()};
    }

    return {};  // 未找到
}
