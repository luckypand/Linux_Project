// ============================================================================
// BridgeConfig.cpp — ROS Bridge YAML 配置解析 实现
// ============================================================================

#include "BridgeConfig.hpp"
#include "RosCompat.hpp"
#include <algorithm>

// ============================================================================
// 方向字符串 → 枚举
// ============================================================================
TopicMappingConfig::Direction BridgeConfig::parseDirection(const std::string& dir)
{
    std::string lower = dir;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "publish")   return TopicMappingConfig::Direction::Publish;
    if (lower == "subscribe") return TopicMappingConfig::Direction::Subscribe;

    // 默认为 subscribe（安全侧：只读不写）
    BRIDGE_ROS_WARN("[BridgeConfig] Unknown direction '%s', defaulting to subscribe", dir.c_str());
    return TopicMappingConfig::Direction::Subscribe;
}

// ============================================================================
// 解析 Topic 映射列表
// ============================================================================
bool BridgeConfig::parseTopics(const YAML::Node& topicsNode)
{
    if (!topicsNode.IsSequence()) {
        BRIDGE_ROS_WARN("[BridgeConfig] 'topics' field is not a sequence, skipping");
        return true;  // 空列表不是错误
    }

    for (const auto& item : topicsNode) {
        TopicMappingConfig cfg;

        // 必填字段
        if (!item["ros_topic"]) {
            BRIDGE_ROS_WARN("[BridgeConfig] Topic entry missing 'ros_topic', skipping");
            continue;
        }
        cfg.rosTopic = item["ros_topic"].as<std::string>();

        if (item["ros_type"]) {
            cfg.rosType = item["ros_type"].as<std::string>();
        }
        if (item["direction"]) {
            cfg.direction = parseDirection(item["direction"].as<std::string>());
        }
        if (item["rpc_service"]) {
            cfg.rpcService = item["rpc_service"].as<std::string>();
        }
        if (item["rpc_method"]) {
            cfg.rpcMethod = item["rpc_method"].as<std::string>();
        }

        // 可选字段
        if (item["rate_hz"]) {
            cfg.rateHz = item["rate_hz"].as<int>();
        }
        if (item["use_shm"]) {
            cfg.useShm = item["use_shm"].as<bool>();
        }
        if (item["use_shm_topic"]) {
            cfg.useShmTopic = item["use_shm_topic"].as<bool>();
        }
        if (item["queue_size"]) {
            cfg.queueSize = item["queue_size"].as<int>();
        }

        topics.push_back(std::move(cfg));

        BRIDGE_ROS_INFO("[BridgeConfig]   topic: %s [%s] -> %s.%s (%s)",
                 cfg.rosTopic.c_str(), cfg.rosType.c_str(),
                 cfg.rpcService.c_str(), cfg.rpcMethod.c_str(),
                 cfg.direction == TopicMappingConfig::Direction::Subscribe ? "ROS→CoreX" : "CoreX→ROS");
    }

    return true;
}

// ============================================================================
// 解析 Service 映射列表
// ============================================================================
bool BridgeConfig::parseServices(const YAML::Node& servicesNode)
{
    if (!servicesNode.IsSequence()) {
        return true;
    }

    for (const auto& item : servicesNode) {
        ServiceMappingConfig cfg;

        if (!item["ros_service"] || !item["rpc_service"] || !item["rpc_method"]) {
            BRIDGE_ROS_WARN("[BridgeConfig] Service entry missing required fields, skipping");
            continue;
        }

        cfg.rosService  = item["ros_service"].as<std::string>();
        cfg.rpcService  = item["rpc_service"].as<std::string>();
        cfg.rpcMethod   = item["rpc_method"].as<std::string>();

        if (item["timeout_ms"]) {
            cfg.timeoutMs = item["timeout_ms"].as<int>();
        }

        services.push_back(std::move(cfg));

        BRIDGE_ROS_INFO("[BridgeConfig]   service: %s -> %s.%s",
                 cfg.rosService.c_str(), cfg.rpcService.c_str(), cfg.rpcMethod.c_str());
    }

    return true;
}

// ============================================================================
// 解析 Action 映射列表
// ============================================================================
bool BridgeConfig::parseActions(const YAML::Node& actionsNode)
{
    if (!actionsNode.IsSequence()) {
        return true;
    }

    for (const auto& item : actionsNode) {
        ActionMappingConfig cfg;

        if (!item["ros_action"] || !item["ros_action_type"] ||
            !item["rpc_service"]) {
            BRIDGE_ROS_WARN("[BridgeConfig] Action entry missing required fields, skipping");
            continue;
        }

        cfg.rosAction          = item["ros_action"].as<std::string>();
        cfg.rosActionType      = item["ros_action_type"].as<std::string>();
        cfg.rpcService         = item["rpc_service"].as<std::string>();
        cfg.rpcMethodStart     = item["rpc_method_start"]
                                     ? item["rpc_method_start"].as<std::string>() : "Start";
        cfg.rpcMethodCancel    = item["rpc_method_cancel"]
                                     ? item["rpc_method_cancel"].as<std::string>() : "Cancel";
        cfg.rpcMethodFeedback  = item["rpc_method_feedback"]
                                     ? item["rpc_method_feedback"].as<std::string>() : "GetFeedback";
        cfg.rpcMethodResult    = item["rpc_method_result"]
                                     ? item["rpc_method_result"].as<std::string>() : "GetResult";

        if (item["timeout_ms"]) {
            cfg.timeoutMs = item["timeout_ms"].as<int>();
        }

        actions.push_back(std::move(cfg));

        BRIDGE_ROS_INFO("[BridgeConfig]   action: %s [%s] -> %s",
                 cfg.rosAction.c_str(), cfg.rosActionType.c_str(),
                 cfg.rpcService.c_str());
    }

    return true;
}

// ============================================================================
// 主解析入口
// ============================================================================
bool BridgeConfig::loadFromYaml(const YAML::Node& rosBridgeNode)
{
    if (!rosBridgeNode || rosBridgeNode.IsNull()) {
        BRIDGE_ROS_INFO("[BridgeConfig] No 'ros_bridge' section in config, bridge disabled");
        enabled = false;
        return true;  // 没有配置不是错误
    }

    enabled = true;

    // ---- 节点配置 ----
    if (rosBridgeNode["node_name"]) {
        nodeName = rosBridgeNode["node_name"].as<std::string>();
    }
    if (rosBridgeNode["spinner_threads"]) {
        spinnerThreads = rosBridgeNode["spinner_threads"].as<int>();
    }
    if (rosBridgeNode["master_uri"]) {
        masterUri = rosBridgeNode["master_uri"].as<std::string>();
    }

    // 强制 disable（用于测试环境）
    if (rosBridgeNode["enabled"]) {
        enabled = rosBridgeNode["enabled"].as<bool>();
    }

    BRIDGE_ROS_INFO("[BridgeConfig] Loading bridge config: node='%s', spinner=%d, enabled=%d",
             nodeName.c_str(), spinnerThreads, enabled);

    // ---- 解析映射表（安全访问，避免 YAML 异常）----
    if (rosBridgeNode["topics"] && rosBridgeNode["topics"].IsDefined()) {
        parseTopics(rosBridgeNode["topics"]);
    }
    if (rosBridgeNode["services"] && rosBridgeNode["services"].IsDefined()) {
        parseServices(rosBridgeNode["services"]);
    }
    if (rosBridgeNode["actions"] && rosBridgeNode["actions"].IsDefined()) {
        parseActions(rosBridgeNode["actions"]);
    }

    BRIDGE_ROS_INFO("[BridgeConfig] Loaded: %zu topic(s), %zu service(s), %zu action(s)",
             topics.size(), services.size(), actions.size());

    return true;
}

// ============================================================================
// 配置校验
// ============================================================================
bool BridgeConfig::validate(std::string& errorMsg) const
{
    if (!enabled) return true;  // 未启用无需校验

    // 校验 Topic 映射
    for (size_t i = 0; i < topics.size(); ++i) {
        const auto& t = topics[i];
        if (t.rosTopic.empty()) {
            errorMsg = "Topic[" + std::to_string(i) + "]: ros_topic is empty";
            return false;
        }
        if (t.rpcService.empty() || t.rpcMethod.empty()) {
            errorMsg = "Topic[" + t.rosTopic + "]: rpc_service or rpc_method is empty";
            return false;
        }
    }

    // 校验 Service 映射
    for (size_t i = 0; i < services.size(); ++i) {
        const auto& s = services[i];
        if (s.rosService.empty() || s.rpcService.empty() || s.rpcMethod.empty()) {
            errorMsg = "Service[" + std::to_string(i) + "]: required field is empty";
            return false;
        }
    }

    // 校验 Action 映射
    for (size_t i = 0; i < actions.size(); ++i) {
        const auto& a = actions[i];
        if (a.rosAction.empty() || a.rosActionType.empty() || a.rpcService.empty()) {
            errorMsg = "Action[" + std::to_string(i) + "]: required field is empty";
            return false;
        }
    }

    return true;
}
