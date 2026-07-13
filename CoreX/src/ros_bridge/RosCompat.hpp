#pragma once
// ============================================================================
// RosCompat.hpp — ROS 兼容层
//
// 当 ROS 不可用时（非机器人平台），提供空实现宏，
// 使 Bridge 的配置解析等非 ROS 依赖功能仍然可以编译。
// ============================================================================

#ifdef HAS_ROS_ENV
// ROS 可用：使用原生 ROS 日志
#include <ros/ros.h>
#define BRIDGE_ROS_INFO(...)  ROS_INFO(__VA_ARGS__)
#define BRIDGE_ROS_WARN(...)  ROS_WARN(__VA_ARGS__)
#define BRIDGE_ROS_ERROR(...) ROS_ERROR(__VA_ARGS__)
#else
// ROS 不可用：降级为 printf
#include <cstdio>
#define BRIDGE_ROS_INFO(fmt, ...)  printf("[BridgeConfig-INFO] " fmt "\n", ##__VA_ARGS__)
#define BRIDGE_ROS_WARN(fmt, ...)  printf("[BridgeConfig-WARN] " fmt "\n", ##__VA_ARGS__)
#define BRIDGE_ROS_ERROR(fmt, ...) printf("[BridgeConfig-ERROR] " fmt "\n", ##__VA_ARGS__)
#endif
