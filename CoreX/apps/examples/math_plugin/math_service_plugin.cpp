// ============================================================================
// Math Service Plugin — 示例业务插件
//
// 编译为 .so 后放入 CoreXDaemon 的 plugins 目录即可自动加载：
//   g++ -shared -fPIC -o libmath_service.so math_service_plugin.cpp \
//       -I../../../src/rpc -I../../../src/net -I../../../proto \
//       $(pkg-config --cflags --libs protobuf)
//
// 或者使用 build.sh 构建:
//   cd /root/Cplus/CoreX
//   ./build.sh math_plugin
// ============================================================================

#include "../../../src/rpc/RpcServiceAdapter.hpp"
#include "../../../proto/math_service.pb.h"
#include <iostream>

// ============================================================================
// MathServiceImpl — 继承 RpcServiceAdapter，提供 Add/Sub 两个 RPC 方法
// ============================================================================
class MathServiceImpl : public RpcServiceAdapter
{
public:
    MathServiceImpl()
        : RpcServiceAdapter(
              // 从生成的 pb 文件中获取 ServiceDescriptor
              CoreX::rpc::MathRequest::descriptor()
                  ->file()->FindServiceByName("MathService")
          )
    {
        registerHandler("Add",
            [](::google::protobuf::Message* reqMsg,
               ::google::protobuf::Message* respMsg)
            {
                auto* req  = static_cast<CoreX::rpc::MathRequest*>(reqMsg);
                auto* resp = static_cast<CoreX::rpc::MathResponse*>(respMsg);
                resp->set_result(req->a() + req->b());
                resp->set_success(true);
            }
        );

        registerHandler("Sub",
            [](::google::protobuf::Message* reqMsg,
               ::google::protobuf::Message* respMsg)
            {
                auto* req  = static_cast<CoreX::rpc::MathRequest*>(reqMsg);
                auto* resp = static_cast<CoreX::rpc::MathResponse*>(respMsg);
                resp->set_result(req->a() - req->b());
                resp->set_success(true);
            }
        );
    }
};

// ============================================================================
// 插件导出函数 — PluginLoader 通过 dlsym 查找这些符号
// ============================================================================

extern "C" {

// 创建服务实例（返回基类指针）
RpcServiceAdapter* createService()
{
    return new MathServiceImpl();
}

// 销毁服务实例
void destroyService(RpcServiceAdapter* svc)
{
    delete svc;
}

}  // extern "C"
