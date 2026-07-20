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

    // ---- ★ 新增: robot_id ----
    if (rosBridgeNode["robot_id"]) {
        robotId = rosBridgeNode["robot_id"].as<std::string>();
    }

    // ---- ★ 新增: ROS 消息类型注册表 ----
    if (rosBridgeNode["ros_message_types"] && rosBridgeNode["ros_message_types"].IsDefined()) {
        parseMessageTypes(rosBridgeNode["ros_message_types"]);
    }

    // ---- ★ 新增: 通用动作映射 ----
    if (rosBridgeNode["generic_actions"] && rosBridgeNode["generic_actions"].IsDefined()) {
        parseGenericActions(rosBridgeNode["generic_actions"]);
    }

    BRIDGE_ROS_INFO("[BridgeConfig] Loaded: %zu topic(s), %zu service(s), %zu action(s), "
             "%zu generic_action(s), %zu message_type(s), robot_id='%s'",
             topics.size(), services.size(), actions.size(),
             genericActions.size(), messageTypes.size(), robotId.c_str());

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

    // 校验 GenericAction 映射
    for (size_t i = 0; i < genericActions.size(); ++i) {
        const auto& ga = genericActions[i];
        if (ga.action.empty()) {
            errorMsg = "GenericAction[" + std::to_string(i) + "]: action is empty";
            return false;
        }
        if (ga.rosTopic.empty() || ga.rosType.empty()) {
            errorMsg = "GenericAction[" + ga.action + "]: ros_topic or ros_type is empty";
            return false;
        }
    }

    return true;
}

// ============================================================================
// ★ 新增: 解析 ros_message_types 段
// ============================================================================
bool BridgeConfig::parseMessageTypes(const YAML::Node& msgTypesNode)
{
    if (!msgTypesNode.IsMap()) {
        return true;
    }

    for (const auto& entry : msgTypesNode) {
        std::string typeName = entry.first.as<std::string>();
        RosMessageTypeInfo info;
        if (entry.second["md5"]) {
            info.md5 = entry.second["md5"].as<std::string>();
        }
        if (entry.second["datatype"]) {
            info.datatype = entry.second["datatype"].as<std::string>();
        } else {
            info.datatype = typeName;
        }
        messageTypes[typeName] = info;
    }

    return true;
}

// ============================================================================
// ★ 新增: 解析 generic_actions 段
// ============================================================================
bool BridgeConfig::parseGenericActions(const YAML::Node& actionsNode)
{
    if (!actionsNode.IsSequence()) {
        return true;
    }

    for (const auto& item : actionsNode) {
        GenericActionConfig cfg;
        cfg.action = item["action"].as<std::string>();

        if (item["ros_topic"]) {
            cfg.rosTopic = item["ros_topic"].as<std::string>();
        }
        if (item["ros_type"]) {
            cfg.rosType = item["ros_type"].as<std::string>();
        }
        if (item["direction"]) {
            std::string dir = item["direction"].as<std::string>();
            cfg.isPublisher = (dir != "subscribe");
        }

        if (item["fields"] && item["fields"].IsSequence()) {
            parseActionFields(item["fields"], cfg.fields);
        }

        genericActions.push_back(std::move(cfg));
    }

    return true;
}

// ============================================================================
// ★ 新增: 递归解析字段列表（支持 nested_fields）
// ============================================================================
bool BridgeConfig::parseActionFields(const YAML::Node& fieldsNode,
                                      std::vector<GenericActionFieldConfig>& outFields)
{
    for (const auto& f : fieldsNode) {
        GenericActionFieldConfig fc;
        fc.fieldNumber = f["field_number"].as<int>();

        if (f["type"])     fc.type     = f["type"].as<std::string>();
        if (f["param"])    fc.param    = f["param"].as<std::string>();
        if (f["default"])  fc.defaultValue = f["default"].as<std::string>();
        if (f["required"]) fc.required = f["required"].as<bool>();

        // 递归解析嵌套
        if (f["nested_fields"] && f["nested_fields"].IsSequence()) {
            parseActionFields(f["nested_fields"], fc.nestedFields);
        }

        outFields.push_back(std::move(fc));
    }

    return true;
}
