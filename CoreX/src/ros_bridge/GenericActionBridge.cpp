// ============================================================================
// GenericActionBridge.cpp — 通用动作适配器 实现
// ============================================================================

#include "GenericActionBridge.hpp"
#include "DynamicServiceAdapter.hpp"
#include "RosNodeManager.hpp"
#include "RosMessageSerializer.hpp"
#include "RosSerializedMessageTraits.hpp"

#include <generic_action.pb.h>    // GenericCommand, GenericResponse
#include <ros/ros.h>
#include <cstring>
#include <cmath>
#include <cstdlib>

// ============================================================================
// 构造函数
// ============================================================================
GenericActionBridge::GenericActionBridge(const BridgeConfig& config,
                                           const MessageTypeRegistry& typeRegistry)
    : config_(config)
    , typeRegistry_(typeRegistry)
{
    adapter_ = std::make_unique<DynamicServiceAdapter>("CoreX.rpc.GenericAction");
    adapter_->registerMethod("Execute",
        [this](const std::string& payload) -> std::string {
            return this->handleExecute(payload);
        });
}

GenericActionBridge::~GenericActionBridge()
{
    stop();
}

// ============================================================================
// 生命周期
// ============================================================================
bool GenericActionBridge::start()
{
    if (started_.load()) return true;

    ROS_INFO("[GenericActionBridge] Starting with %zu action(s) configured",
             config_.genericActions.size());

    started_.store(true);
    return true;
}

void GenericActionBridge::stop()
{
    if (!started_.load()) return;

    for (auto& [topic, entry] : publishers_) {
        if (entry.valid) {
            entry.publisher.shutdown();
        }
    }
    publishers_.clear();

    started_.store(false);
    ROS_INFO("[GenericActionBridge] Stopped");
}

// ============================================================================
// RPC Handler: Execute
// ============================================================================
std::string GenericActionBridge::handleExecute(const std::string& payload)
{
    // 1. 反序列化 GenericCommand
    CoreX::rpc::GenericCommand cmd;
    if (!cmd.ParseFromString(payload)) {
        return buildErrorResponse("Failed to parse GenericCommand");
    }

    const std::string& action = cmd.action();
    const std::string& params = cmd.params();

    // 2. 查找 action 配置
    const GenericActionConfig* actionCfg = nullptr;
    for (const auto& ac : config_.genericActions) {
        if (ac.action == action) {
            actionCfg = &ac;
            break;
        }
    }

    if (!actionCfg) {
        return buildErrorResponse("Unknown action: " + action);
    }

    // 3. Publish 方向
    if (actionCfg->isPublisher) {
        std::string rosBytes = buildMessageBytes(actionCfg->fields, params);

        if (rosBytes.empty()) {
            return buildErrorResponse("Failed to build ROS message for action: "
                                      + action);
        }

        auto* pub = getOrCreatePublisher(actionCfg->rosTopic, actionCfg->rosType);
        if (!pub) {
            return buildErrorResponse("Publisher not ready for topic: "
                                      + actionCfg->rosTopic);
        }

        try {
            ros::SerializedMessage serMsg;
            serMsg.num_bytes = rosBytes.size();
            serMsg.buf.reset(new uint8_t[rosBytes.size()]);
            serMsg.message_start = serMsg.buf.get();
            memcpy(serMsg.buf.get(), rosBytes.data(), rosBytes.size());

            pub->publish(serMsg);

            ROS_INFO("[GenericActionBridge] Action '%s' published to %s (%zu bytes)",
                     action.c_str(), actionCfg->rosTopic.c_str(), rosBytes.size());

            return buildSuccessResponse("Published to " + actionCfg->rosTopic);

        } catch (const ros::Exception& e) {
            return buildErrorResponse(std::string("ROS publish error: ") + e.what());
        }

    } else {
        // Subscribe 方向：返回缓存数据
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = actionCache_.find(action);
        if (it != actionCache_.end()) {
            return buildSuccessResponse("OK", it->second);
        }
        return buildErrorResponse("No data available for action: " + action);
    }
}

// ============================================================================
// 递归构建 ROS 消息二进制字节
// ============================================================================
std::string GenericActionBridge::buildMessageBytes(
    const std::vector<GenericActionFieldConfig>& fields,
    const std::string& jsonParams)
{
    std::string buf;
    for (const auto& fc : fields) {
        if (fc.type == "message") {
            // 嵌套消息: 先构造子消息字节，再作为 length-delimited 写入
            std::string nested = buildMessageBytes(fc.nestedFields, jsonParams);
            RosMessageSerializer::writeTag(buf, fc.fieldNumber, 2);
            RosMessageSerializer::writeVarint(buf,
                static_cast<uint64_t>(nested.size()));
            buf.append(nested);
        } else {
            writeFieldByType(buf, fc, jsonParams);
        }
    }
    return buf;
}

// ============================================================================
// 按类型写入单个字段
// ============================================================================
void GenericActionBridge::writeFieldByType(std::string& buf,
    const GenericActionFieldConfig& fc, const std::string& jsonParams)
{
    const std::string& t = fc.type;

    if (t == "float64") {
        double val = getJsonDouble(jsonParams, fc.param,
            fc.defaultValue.empty() ? 0.0 : std::stod(fc.defaultValue));
        RosMessageSerializer::writeFloat64(buf, fc.fieldNumber, val);
    }
    else if (t == "float32") {
        float val = static_cast<float>(getJsonDouble(jsonParams, fc.param,
            fc.defaultValue.empty() ? 0.0f : std::stof(fc.defaultValue)));
        RosMessageSerializer::writeFloat32(buf, fc.fieldNumber, val);
    }
    else if (t == "int32") {
        int32_t val = static_cast<int32_t>(getJsonInt(jsonParams, fc.param,
            fc.defaultValue.empty() ? 0 : std::stoll(fc.defaultValue)));
        RosMessageSerializer::writeInt32(buf, fc.fieldNumber, val);
    }
    else if (t == "uint32") {
        uint32_t val = static_cast<uint32_t>(getJsonInt(jsonParams, fc.param,
            fc.defaultValue.empty() ? 0 : std::stoull(fc.defaultValue)));
        RosMessageSerializer::writeUInt32(buf, fc.fieldNumber, val);
    }
    else if (t == "bool") {
        bool val = getJsonBool(jsonParams, fc.param,
            fc.defaultValue == "true");
        RosMessageSerializer::writeBool(buf, fc.fieldNumber, val);
    }
    else if (t == "string") {
        std::string val = getJsonString(jsonParams, fc.param, fc.defaultValue);
        RosMessageSerializer::writeString(buf, fc.fieldNumber, val);
    }
    else {
        ROS_WARN("[GenericActionBridge] Unknown field type '%s' for field %d",
                 t.c_str(), fc.fieldNumber);
    }
}

// ============================================================================
// 获取或创建 ROS Publisher
// ============================================================================
ros::Publisher* GenericActionBridge::getOrCreatePublisher(
    const std::string& topic, const std::string& rosType)
{
    auto it = publishers_.find(topic);
    if (it != publishers_.end() && it->second.valid) {
        return &it->second.publisher;
    }

    auto traits = typeRegistry_.lookup(rosType);
    auto& nodeMgr = RosNodeManager::instance();

    try {
        ros::AdvertiseOptions ops;
        ops.topic      = topic;
        ops.queue_size = 10;
        ops.datatype   = traits.datatype;
        ops.md5sum     = traits.md5;

        auto& entry = publishers_[topic];
        entry.publisher = nodeMgr.nh().advertise(ops);
        entry.valid = true;

        ROS_INFO("[GenericActionBridge] Publisher ready for %s [%s]",
                 topic.c_str(), rosType.c_str());
        return &entry.publisher;

    } catch (const ros::Exception& e) {
        ROS_ERROR("[GenericActionBridge] Failed to create publisher for %s: %s",
                  topic.c_str(), e.what());
        return nullptr;
    }
}

// ============================================================================
// 简易 JSON 解析器 (key-value 级别，无外部依赖)
// ============================================================================

static bool jsonFindKey(const std::string& json, const std::string& key,
                        size_t& valStart, size_t& valEnd)
{
    // 查找 "key" 的位置
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;

    // 跳过 "key":
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return false;
    pos++;  // 跳过 ':'

    // 跳过空白
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'
           || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }

    if (pos >= json.size()) return false;

    valStart = pos;

    // 确定值的结束位置
    if (json[pos] == '"') {
        // 字符串值
        pos++;
        valStart = pos;
        while (pos < json.size() && json[pos] != '"') pos++;
        valEnd = pos;
    } else if (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9')) {
        // 数字值
        valStart = pos;
        while (pos < json.size() && (json[pos] == '-' || json[pos] == '.'
               || json[pos] == 'e' || json[pos] == 'E' || json[pos] == '+'
               || (json[pos] >= '0' && json[pos] <= '9'))) {
            pos++;
        }
        valEnd = pos;
    } else if (json[pos] == 't' || json[pos] == 'f') {
        // 布尔值
        valStart = pos;
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}'
               && json[pos] != ' ' && json[pos] != '\n') {
            pos++;
        }
        valEnd = pos;
    } else {
        return false;
    }

    return true;
}

double GenericActionBridge::getJsonDouble(const std::string& json,
    const std::string& key, double defVal)
{
    size_t start, end;
    if (!jsonFindKey(json, key, start, end)) return defVal;
    return std::stod(json.substr(start, end - start));
}

int64_t GenericActionBridge::getJsonInt(const std::string& json,
    const std::string& key, int64_t defVal)
{
    size_t start, end;
    if (!jsonFindKey(json, key, start, end)) return defVal;
    return std::stoll(json.substr(start, end - start));
}

std::string GenericActionBridge::getJsonString(const std::string& json,
    const std::string& key, const std::string& defVal)
{
    size_t start, end;
    if (!jsonFindKey(json, key, start, end)) return defVal;
    return json.substr(start, end - start);
}

bool GenericActionBridge::getJsonBool(const std::string& json,
    const std::string& key, bool defVal)
{
    size_t start, end;
    if (!jsonFindKey(json, key, start, end)) return defVal;
    std::string val = json.substr(start, end - start);
    return (val == "true" || val == "True" || val == "1");
}

// ============================================================================
// 辅助：构造响应
// ============================================================================
std::string GenericActionBridge::buildErrorResponse(const std::string& errMsg)
{
    CoreX::rpc::GenericResponse resp;
    resp.set_success(false);
    resp.set_error_msg(errMsg);
    ROS_WARN("[GenericActionBridge] Error: %s", errMsg.c_str());
    return resp.SerializeAsString();
}

std::string GenericActionBridge::buildSuccessResponse(const std::string& msg,
                                                       const std::string& data)
{
    CoreX::rpc::GenericResponse resp;
    resp.set_success(true);
    resp.set_message(msg);
    if (!data.empty()) {
        resp.set_data(data);
    }
    return resp.SerializeAsString();
}
