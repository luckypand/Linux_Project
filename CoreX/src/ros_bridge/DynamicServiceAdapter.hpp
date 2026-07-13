#pragma once
// ============================================================================
// DynamicServiceAdapter.hpp — 动态 RPC Service 适配器
//
// 与标准 RpcServiceAdapter 的区别：
//   - 不需要编译期 .proto 生成的 ServiceDescriptor
//   - dispatch() 直接通过 methodName 查 handler map
//   - serviceName() 由配置指定（而非 ServiceDescriptor::full_name()）
//   - 适用于配置驱动的 Bridge 适配器
//
// 使用方式：
//   DynamicServiceAdapter adapter("CoreX.rpc.RobotTelemetry");
//   adapter.registerMethod("GetOdometry",
//       [](const std::string& payload) -> std::string { ... });
//   // 注册到 RpcServer
//   server.registerService(&adapter);
// ============================================================================

#include "../rpc/RpcServiceAdapter.hpp"
#include <string>
#include <unordered_map>
#include <functional>

class DynamicServiceAdapter : public RpcServiceAdapter
{
public:
    // 动态 handler：接收原始 payload bytes，返回原始 response bytes
    // 不经过 protobuf Message 反序列化/序列化（由调用方自行处理）
    using DynamicHandler = std::function<std::string(const std::string& payload)>;

    // 构造函数
    // @param svcName  RPC Service 全名，如 "CoreX.rpc.RobotTelemetry"
    explicit DynamicServiceAdapter(const std::string& svcName)
        : RpcServiceAdapter(nullptr)  // 不使用标准 ServiceDescriptor
        , serviceName_(svcName)
    {}

    // 覆盖 serviceName()
    const std::string& serviceName() const override { return serviceName_; }

    // 覆盖 dispatch()：直接查 handler map，不走 protobuf 反序列化
    std::string dispatch(const std::string& methodName,
                         const std::string& payload) override
    {
        auto it = handlers_.find(methodName);
        if (it == handlers_.end()) {
            return "";  // 方法未注册
        }
        return it->second(payload);
    }

    // 注册动态方法
    // @param methodName  RPC 方法名
    // @param handler     处理函数：接收 payload bytes → 返回 response bytes
    void registerMethod(const std::string& methodName, DynamicHandler handler)
    {
        handlers_[methodName] = std::move(handler);
    }

    // 查询已注册的方法列表
    std::vector<std::string> registeredMethods() const
    {
        std::vector<std::string> names;
        for (const auto& kv : handlers_) {
            names.push_back(kv.first);
        }
        return names;
    }

private:
    std::string serviceName_;
    std::unordered_map<std::string, DynamicHandler> handlers_;
};
