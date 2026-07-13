#pragma once
// ============================================================================
// RosProtobufConv.hpp — ROS Message ↔ Protobuf 转换工具
//
// 提供常用 ROS 消息类型与 Protobuf 消息之间的双向转换函数
//
// 当前支持的类型映射：
//   geometry_msgs/Twist        ↔ VelocityCommand
//   nav_msgs/Odometry          ↔ OdometryResponse
//   sensor_msgs/JointState     ↔ JointState
//   sensor_msgs/LaserScan      ↔ LaserScan
//   sensor_msgs/Image          ↔ Image
//   sensor_msgs/PointCloud2    ↔ PointCloud2
//   tf2_msgs/TFMessage         ↔ TFMessage
// ============================================================================

#include <string>

// ============================================================================
// 通用转换接口
// ============================================================================

// 序列化 ROS 消息 → bytes（用于通用 Subscribe 缓存）
// 使用 ros::serialization::serialize 将任意 ROS 消息序列化为字节流
template<typename RosMsg>
std::string serializeRosMessage(const RosMsg& msg);

// 反序列化 bytes → ROS 消息（用于通用 Publish）
template<typename RosMsg>
bool deserializeRosMessage(const std::string& data, RosMsg& msg);

// ============================================================================
// 类型特定转换函数声明
// （这些函数在 .cpp 中实现，依赖对应的 ROS 消息头文件）
// ============================================================================

#ifdef HAS_ROS_CONVERSIONS  // 编译开关：仅在 ROS 环境下编译

#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/JointState.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/PointCloud2.h>

// 注意：依赖 robot_service.pb.h 和 ros_messages.pb.h
// 这些头文件在 CMake 中包含

namespace RosProtobufConv {

// geometry_msgs/Twist ↔ VelocityCommand
void twistToProto(const geometry_msgs::Twist& ros, /* VelocityCommand& */ void* proto);
void protoToTwist(const void* proto, geometry_msgs::Twist& ros);

// nav_msgs/Odometry ↔ OdometryResponse
void odomToProto(const nav_msgs::Odometry& ros, void* proto);

// sensor_msgs/JointState ↔ JointState
void jointStateToProto(const sensor_msgs::JointState& ros, void* proto);

// sensor_msgs/Image ↔ Image
void imageToProto(const sensor_msgs::Image& ros, void* proto);

// sensor_msgs/LaserScan ↔ LaserScan
void laserScanToProto(const sensor_msgs::LaserScan& ros, void* proto);

// sensor_msgs/PointCloud2 ↔ PointCloud2
void pointCloudToProto(const sensor_msgs::PointCloud2& ros, void* proto);

}  // namespace RosProtobufConv

#endif  // HAS_ROS_CONVERSIONS
