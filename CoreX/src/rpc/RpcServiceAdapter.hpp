#pragma once
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <string>
#include <unordered_map>
#include <functional>

// 通用 Service 适配器 —— 一个 Service 实例对应一个 proto service
// 利用 protoc 生成的 ServiceDescriptor 元数据做动态分发
class RpcServiceAdapter
{
public:
    // handler: void(RequestType&, ResponseType&)
    using MethodHandler = std::function<void(
        ::google::protobuf::Message* request,
        ::google::protobuf::Message* response
    )>;

    // 构造时传入 ServiceDescriptor（从生成的 .pb.h 获取）
    explicit RpcServiceAdapter(const ::google::protobuf::ServiceDescriptor* svcDesc)
        : svcDesc_(svcDesc)
    {}

    virtual ~RpcServiceAdapter() = default;

    const std::string& serviceName() const { return svcDesc_->full_name(); }

    // 注册一个方法的处理函数（由子类在构造函数中调用）
    void registerHandler(const std::string& methodName, MethodHandler handler)
    {
        handlers_[methodName] = std::move(handler);
    }

    // ★ 核心：根据方法名，创建 request、解析 payload、调用 handler、返回序列化 response
    // 返回空 string 表示出错
    std::string dispatch(const std::string& methodName,
                         const std::string& payload)
    {
        // 1. 查找 MethodDescriptor
        const ::google::protobuf::MethodDescriptor* method =
            svcDesc_->FindMethodByName(methodName);
        if (!method) return "";

        // 2. 查找 handler
        auto it = handlers_.find(methodName);
        if (it == handlers_.end()) return "";

        // 3. ★ 动态创建 request 对象（不需要知道具体类型！）
        std::unique_ptr<::google::protobuf::Message> request(
            GetRequestPrototype(method).New()
        );

        // 4. 反序列化
        if (!request->ParseFromString(payload)) return "";

        // 5. ★ 动态创建 response 对象
        std::unique_ptr<::google::protobuf::Message> response(
            GetResponsePrototype(method).New()
        );

        // 6. 调用 handler
        it->second(request.get(), response.get());

        // 7. 序列化返回
        return response->SerializeAsString();
    }

    // 获取方法的默认 request/response 实例（用于 New()）
    const ::google::protobuf::Message& GetRequestPrototype(
        const ::google::protobuf::MethodDescriptor* method) const
    {
        return *::google::protobuf::MessageFactory::generated_factory()
                    ->GetPrototype(method->input_type());
    }

    const ::google::protobuf::Message& GetResponsePrototype(
        const ::google::protobuf::MethodDescriptor* method) const
    {
        return *::google::protobuf::MessageFactory::generated_factory()
                    ->GetPrototype(method->output_type());
    }

private:
    const ::google::protobuf::ServiceDescriptor* svcDesc_;
    std::unordered_map<std::string, MethodHandler> handlers_;
};
