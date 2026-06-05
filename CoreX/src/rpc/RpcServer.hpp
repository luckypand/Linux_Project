#pragma once
#include "../net/TcpServer.hpp"
#include "../net/EventLoop.hpp"
#include "RpcCodec.hpp"
#include "../../proto/rpc_message.pb.h"
#include "../../proto/math_service.pb.h"
#include <unordered_map>
#include <functional>
#include "RpcServiceAdapter.hpp"

using RpcMethodCallback = std::function<void()>;

class RpcServer
{
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
    RpcServer(EventLoop* loop,const std::string& ip,uint16_t port,const std::string& name);
    void start() { server_.start(); }

    //业务接口，用户在main函数中注册函数
    void registerService(RpcServiceAdapter* adapter);
private:
    void sendResponse(const TcpConnectionPtr& conn, uint64_t id,const std::string& rstPayload);
    void sendErrorReasponse(const TcpConnectionPtr& conn,
                           uint64_t id, CoreX::rpc::ErrorCode code, const std::string& errMsg);
    void handleRpcCodecMessage(const TcpConnectionPtr& conn,const std::string& payload);  
    TcpServer server_;
    RpcCodec codec_;
    std::unordered_map<std::string,RpcServiceAdapter*> dispatchTable_;
};

//server_的Onmessage  ->  实际调用RpcCodec内部的Onmessage
//RpcCodec的businessCallback_ -> 实际调用RpcServer的handleRpcMessage