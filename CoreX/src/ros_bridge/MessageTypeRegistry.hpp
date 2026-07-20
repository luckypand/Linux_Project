#pragma once
// ============================================================================
// MessageTypeRegistry.hpp — YAML 驱动的 ROS 消息类型注册表
//
// 提供 MD5/datatype 查询，替代编译期硬编码的 getMessageTraits()。
// 查询优先级:
//   1. YAML 配置中的 ros_message_types（新增类型无需改 C++）
//   2. 编译期已知类型的硬编码回退
//   3. 返回 {"*", rosType} 作为保底
// ============================================================================

#include <string>
#include <unordered_map>

// 前向声明
struct BridgeConfig;

class MessageTypeRegistry
{
public:
    struct TypeTraits
    {
        std::string md5;
        std::string datatype;
    };

    MessageTypeRegistry() = default;

    // 从 BridgeConfig 的 ros_message_types 段加载
    void loadFromConfig(const BridgeConfig& config);

    // 查询消息类型的 MD5 和 datatype
    TypeTraits lookup(const std::string& rosType) const;

    // 手动注册类型（用于测试或运行时扩展）
    void addType(const std::string& rosType,
                 const std::string& md5,
                 const std::string& datatype);

private:
    // 编译期回退：保持对 geometry_msgs/Twist 等已知类型的支持
    static TypeTraits compileTimeFallback(const std::string& rosType);

    std::unordered_map<std::string, TypeTraits> yamlTypes_;
};
