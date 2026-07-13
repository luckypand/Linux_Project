#pragma once
// ============================================================================
// BridgeConfig.hpp — ROS Bridge YAML 配置解析 + 数据结构
//
// 职责：
//   1. 定义 Topic / Service / Action 映射的配置数据结构
//   2. 从 YAML::Node 解析 ros_bridge 配置段
//   3. 提供配置有效性校验
//
// 使用方式：
//   BridgeConfig cfg;
//   if (!cfg.loadFromYaml(yamlNode)) { ... }
//   for (auto& t : cfg.topics) { ... }
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <yaml-cpp/yaml.h>

// ============================================================================
// Topic 映射配置
// ============================================================================
struct TopicMappingConfig
{
    // 方向枚举
    enum class Direction {
        Subscribe,  // ROS → CoreX（Bridge 订阅 ROS topic，缓存数据供 RPC 查询）
        Publish     // CoreX → ROS（RPC 调用触达后，Bridge 发布到 ROS topic）
    };

    std::string rosTopic;       // ROS topic 名称，如 "/cmd_vel"
    std::string rosType;        // ROS 消息类型，如 "geometry_msgs/Twist"
    Direction   direction;      // subscribe 或 publish
    std::string rpcService;     // 对应的 RPC Service 全名，如 "CoreX.rpc.MotionControl"
    std::string rpcMethod;      // 对应的 RPC Method 名，如 "SetVelocity"
    int         rateHz = 0;     // subscribe 方向：缓存更新速率上限 (Hz)，0 表示不限
    bool        useShm  = false;     // 是否使用 SHM 传输大消息（图像/点云，已弃用→见 useShmTopic）
    bool        useShmTopic = false; // ★ 是否启用 SHM Topic 直通（同机节点走共享内存替代 TCPROS）
    int         queueSize = 10;     // ROS subscriber/publisher 队列大小
};

// ============================================================================
// Service 映射配置
// ============================================================================
struct ServiceMappingConfig
{
    std::string rosService;     // ROS Service 名称，如 "/gazebo/spawn_model"
    std::string rpcService;     // 对应的 RPC Service 全名
    std::string rpcMethod;      // 对应的 RPC Method 名
    int         timeoutMs = 5000; // ROS Service 调用超时 (ms)
};

// ============================================================================
// Action 映射配置
// ============================================================================
struct ActionMappingConfig
{
    std::string rosAction;          // ROS Action 名称，如 "/move_base"
    std::string rosActionType;      // ROS Action 类型，如 "move_base_msgs/MoveBaseAction"
    std::string rpcService;         // 对应的 RPC Service 全名
    std::string rpcMethodStart;     // 发起 Goal 的 RPC method
    std::string rpcMethodCancel;    // 取消 Goal 的 RPC method
    std::string rpcMethodFeedback;  // 查询 Feedback 的 RPC method
    std::string rpcMethodResult;    // 查询 Result 的 RPC method
    int         timeoutMs = 30000;  // Action 超时 (ms)
};

// ============================================================================
// BridgeConfig — 总配置结构
// ============================================================================
struct BridgeConfig
{
    // ROS 节点配置
    std::string nodeName       = "corex_ros_bridge";
    int         spinnerThreads = 2;
    std::string masterUri;       // 可选，空串表示从环境变量 ROS_MASTER_URI 读取
    bool        enabled = false; // 是否启用 ROS Bridge（false 时跳过初始化）

    // 映射表
    std::vector<TopicMappingConfig>   topics;
    std::vector<ServiceMappingConfig> services;
    std::vector<ActionMappingConfig>  actions;

    // ---- 解析方法 ----
    // 从 YAML::Node 加载（root node 应为 "ros_bridge" 段）
    bool loadFromYaml(const YAML::Node& rosBridgeNode);

    // 校验配置有效性（返回 true 表示配置合法）
    bool validate(std::string& errorMsg) const;

private:
    // 解析子段
    bool parseTopics(const YAML::Node& topicsNode);
    bool parseServices(const YAML::Node& servicesNode);
    bool parseActions(const YAML::Node& actionsNode);

    // 工具方法
    static TopicMappingConfig::Direction parseDirection(const std::string& dir);
};
