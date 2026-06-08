#pragma once
#include "RpcServiceAdapter.hpp"
#include "../../proto/math_service.pb.h"

class MathServiceImpl : public RpcServiceAdapter
{
public:
    MathServiceImpl()
        : RpcServiceAdapter(
              // ★ 从生成的 pb 文件中获取 ServiceDescriptor
              //    通过任意 message 的 descriptor()->file()->service(0)
              CoreX::rpc::MathRequest::descriptor()
                  ->file()->FindServiceByName("MathService")
          )
    {
        // //test
        // auto* file = CoreX::rpc::MathRequest::descriptor()->file();
        // std::cout << "file name: " << file->name() << std::endl;
        // std::cout << "service count: " << file->service_count() << std::endl;
        // for (int i = 0; i < file->service_count(); i++) {
        //     std::cout << "service[" << i << "]: '" << file->service(i)->full_name() << "'" << std::endl;
        // }
        // auto* svc = file->FindServiceByName("CoreX.rpc.MathService");
        // std::cout << "FindServiceByName result: " << (void*)svc << std::endl;
        // auto* svc2 = file->FindServiceByName("MathService");
        // std::cout << "FindServiceByName(\"MathService\") result: " << (void*)svc2 << std::endl;
        // //test

        // 注册 Add 方法
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

        // 注册 Sub 方法
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
