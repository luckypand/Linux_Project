#pragma once
#include "../net/TcpServer.hpp"
#include "../net/EventLoop.hpp"
#include "RpcCodec.hpp"
#include "RpcLatencyStats.hpp"
#include "../../proto/rpc_message.pb.h"
#include "../../proto/math_service.pb.h"
#include <unordered_map>
#include <functional>
#include "RpcServiceAdapter.hpp"
#include <memory>

class IpcRpcBridge;

using RpcMethodCallback = std::function<void()>;

class RpcServer
{
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
    RpcServer(EventLoop* loop,const std::string& ip,uint16_t port,const std::string& name);
    ~RpcServer();  // 在 .cpp 中定义，因为 unique_ptr<IpcRpcBridge> 需要完整类型
    void start() { server_.start(); }

    // ★ 设置服务端延迟统计收集器（可选，不设置则无开销）
    void setLatencyStats(RpcLatencyStats* stats) { latencyStats_ = stats; }
    RpcLatencyStats* getLatencyStats() const { return latencyStats_; }

    // ★ 启用 IPC fast-path 监听（共享内存 RingBuffer → RPC 管线）
    void enableIpc(const std::string& shmName = "/corex_rpc_ipc");

    //业务接口，用户在main函数中注册函数
    void registerService(RpcServiceAdapter* adapter);
private:
#if ENABLE_TIMESTAMP
    void sendResponse(const TcpConnectionPtr& conn, uint64_t id, const std::string& rstPayload,
                      uint64_t client_send_ts, uint64_t server_recv_ts);
    void sendErrorReasponse(const TcpConnectionPtr& conn,
                           uint64_t id, CoreX::rpc::ErrorCode code, const std::string& errMsg,
                           uint64_t server_recv_ts = 0);
#else
    void sendResponse(const TcpConnectionPtr& conn, uint64_t id, const std::string& rstPayload);
    void sendErrorReasponse(const TcpConnectionPtr& conn,
                           uint64_t id, CoreX::rpc::ErrorCode code, const std::string& errMsg);
#endif
    void handleRpcCodecMessage(const TcpConnectionPtr& conn,const std::string& payload);  
    TcpServer server_;
    RpcCodec codec_;
    std::unordered_map<std::string,RpcServiceAdapter*> dispatchTable_;
    RpcLatencyStats* latencyStats_ = nullptr;  // ★ 非拥有指针，外部管理生命周期

    // ★ IPC fast-path 桥接器（可选，enableIpc() 后非空）
    std::unique_ptr<IpcRpcBridge> ipcBridge_;
};

//server_的Onmessage  ->  实际调用RpcCodec内部的Onmessage
//RpcCodec的businessCallback_ -> 实际调用RpcServer的handleRpcMessage