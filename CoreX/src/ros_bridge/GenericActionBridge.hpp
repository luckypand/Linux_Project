#pragma once
// ============================================================================
// GenericActionBridge.hpp — 通用动作适配器
//
// 职责：
//   1. 接收 GenericCommand RPC 调用
//   2. 按 action 名查找 YAML 配置
//   3. 调用 RosMessageSerializer 动态构造 ROS 消息
//   4. 发布到 ROS topic（或从 subscribe direction 返回缓存数据）
//
// RPC 接口：
//   service: "CoreX.rpc.GenericAction"
//   method:  "Execute"
//   payload: GenericCommand{action, params(JSON)}
// ============================================================================

#include "BridgeConfig.hpp"
#include "MessageTypeRegistry.hpp"
#include "RosMessageSerializer.hpp"

#include <ros/ros.h>
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>

class DynamicServiceAdapter;

class GenericActionBridge
{
public:
    explicit GenericActionBridge(const BridgeConfig& config,
                                  const MessageTypeRegistry& typeRegistry);
    ~GenericActionBridge();

    GenericActionBridge(const GenericActionBridge&) = delete;
    GenericActionBridge& operator=(const GenericActionBridge&) = delete;

    bool start();
    void stop();

    DynamicServiceAdapter* getServiceAdapter() { return adapter_.get(); }

private:
    // ---- RPC Handler ----
    std::string handleExecute(const std::string& payload);

    // ---- 消息构建 ----
    // 递归构建 ROS 消息字节
    std::string buildMessageBytes(
        const std::vector<GenericActionFieldConfig>& fields,
        const std::string& jsonParams);

    // 按类型写入单个字段
    void writeFieldByType(std::string& buf, const GenericActionFieldConfig& fc,
                          const std::string& jsonParams);

    // ---- ROS 发布/订阅 ----
    ros::Publisher* getOrCreatePublisher(const std::string& topic,
                                          const std::string& rosType);

    // ---- JSON 简易解析 (params) ----
    double      getJsonDouble(const std::string& json, const std::string& key,
                              double defVal);
    int64_t     getJsonInt(const std::string& json, const std::string& key,
                           int64_t defVal);
    std::string getJsonString(const std::string& json, const std::string& key,
                              const std::string& defVal);
    bool        getJsonBool(const std::string& json, const std::string& key,
                            bool defVal);

    // ---- 辅助 ----
    std::string buildErrorResponse(const std::string& errMsg);
    std::string buildSuccessResponse(const std::string& msg,
                                     const std::string& data = "");

    // ---- 成员 ----
    BridgeConfig                config_;
    const MessageTypeRegistry&  typeRegistry_;
    std::unique_ptr<DynamicServiceAdapter> adapter_;

    // 发布者缓存 (topic → publisher)
    struct TopicEntry {
        ros::Publisher publisher;
        bool valid = false;
    };
    std::unordered_map<std::string, TopicEntry> publishers_;

    // 缓存 (subscribe 方向，action → raw bytes)
    std::unordered_map<std::string, std::string> actionCache_;
    mutable std::mutex cacheMutex_;

    std::atomic<bool> started_{false};
};
