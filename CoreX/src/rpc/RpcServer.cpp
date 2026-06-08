#include "RpcServer.hpp"
#include <arpa/inet.h>
#if ENABLE_TIMESTAMP
#include <chrono>
#endif
#include "my_log.hpp"

#if ENABLE_TIMESTAMP
// Get current steady_clock timestamp in microseconds
static inline uint64_t now_us() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
#endif

RpcServer::RpcServer(EventLoop* loop,const std::string& ip,uint16_t port,const std::string& name)
    :server_(loop,ip,port,name)
{
    codec_.setbusinessCallback_(
        [this](const TcpConnectionPtr& conn,const std::string& payload){
            this->handleRpcCodecMessage(conn,payload);
        }//实际的路由转发逻辑
    );
    server_.setThreadNum(4);
    server_.setMessageCallback(  //当server接收到客户端数据(此时应该是序列化后的rpc请求),需要反序列化并路由处理
        [this](const TcpConnectionPtr& conn,Buffer& buf){
            codec_.Onmessage(conn,buf);
        }
    );
}

/*
* @brief:
*     此处为RpcCodec中注册的businessCallback_实际执行的逻辑,处理
  单个元数据包rpcMsg和dispatch进行具体业务逻辑,并调用sendresponse返回序列化后的结果
  @param:
        conn    本次连接的Tcpconnection
        payload	反序列化完成的Protobuf字节流
*/
void RpcServer::handleRpcCodecMessage(const TcpConnectionPtr& conn,const std::string& payload)
{
#if ENABLE_TIMESTAMP
    auto t0 = now_us();
#endif

    CoreX::rpc::RpcMessage rpcMsg;
    if(!rpcMsg.ParseFromString(payload))
    {
#if ENABLE_TIMESTAMP
        sendErrorReasponse(conn, 0,
                            CoreX::rpc::WRONG_PROTO,
                            "Fail to parse payload", t0);
#else
        sendErrorReasponse(conn, 0,
                            CoreX::rpc::WRONG_PROTO,
                            "Fail to parse payload");
#endif
        return; //解析失败
    }

#if ENABLE_TIMESTAMP
    auto t1 = now_us();
#endif

    if(rpcMsg.type() != CoreX::rpc::REQUEST)
    {
        return;
    }

#if ENABLE_TIMESTAMP
    // 记录服务端接收时间戳（反序列化完成时刻，更精确反映网络到达时间）
    uint64_t server_recv_ts = t1;
    uint64_t client_send_ts = rpcMsg.client_send_ts();
#endif

    auto it = dispatchTable_.find(rpcMsg.service());

#if ENABLE_TIMESTAMP
    auto t2 = now_us();
#endif

    if(it != dispatchTable_.end())
    {
        std::string result = it->second->dispatch(rpcMsg.method(),rpcMsg.payload());

#if ENABLE_TIMESTAMP
        auto t3 = now_us();
#endif

        if(!result.empty())
        {
#if ENABLE_TIMESTAMP
            sendResponse(conn, rpcMsg.id(), result, client_send_ts, server_recv_ts);
#else
            sendResponse(conn, rpcMsg.id(), result);
#endif
        }

#if ENABLE_TIMESTAMP
        auto t4 = now_us();

        // 异步日志记录各阶段耗时，不阻塞当前请求处理
        LOG_INFO("[RPC-TIMING] id=%lu %s.%s | "
                 "deserialize:%luus route:%luus business:%luus send:%luus | total:%luus",
                 rpcMsg.id(),
                 rpcMsg.service().c_str(),
                 rpcMsg.method().c_str(),
                 t1 - t0,
                 t2 - t1,
                 t3 - t2,
                 t4 - t3,
                 t4 - t0);
#endif
    }
}

// 8 字节 TLV 头部与发送缓冲区
#if ENABLE_TIMESTAMP
void RpcServer::sendResponse(const TcpConnectionPtr& conn, uint64_t id,
                             const std::string& rstPayload,
                             uint64_t client_send_ts, uint64_t server_recv_ts)
{
    //构建响应封装
    CoreX::rpc::RpcMessage rpcMsg;
    rpcMsg.set_type(CoreX::rpc::RESPONSE);
    rpcMsg.set_id(id);
    rpcMsg.set_payload(rstPayload);
    rpcMsg.set_client_send_ts(client_send_ts);
    rpcMsg.set_server_recv_ts(server_recv_ts);
    rpcMsg.set_server_send_ts(now_us());

    std::string serialized = rpcMsg.SerializeAsString();

    uint32_t length = serialized.size();
    uint32_t magic = 0x42414E41; // 魔数 "BANA"

    // 转换为网络字节序
    uint32_t be32_magic = htonl(magic);
    uint32_t be32_length = htonl(length);

    // 组装头部与负载
    std::string packet;
    packet.append(reinterpret_cast<char*>(&be32_magic), 4);
    packet.append(reinterpret_cast<char*>(&be32_length), 4);
    packet.append(serialized);

    // 异步发送至客户端
    conn->send(packet);
}
#else
void RpcServer::sendResponse(const TcpConnectionPtr& conn, uint64_t id,
                             const std::string& rstPayload)
{
    CoreX::rpc::RpcMessage rpcMsg;
    rpcMsg.set_type(CoreX::rpc::RESPONSE);
    rpcMsg.set_id(id);
    rpcMsg.set_payload(rstPayload);

    std::string serialized = rpcMsg.SerializeAsString();

    uint32_t length = serialized.size();
    uint32_t magic = 0x42414E41;

    uint32_t be32_magic = htonl(magic);
    uint32_t be32_length = htonl(length);

    std::string packet;
    packet.append(reinterpret_cast<char*>(&be32_magic), 4);
    packet.append(reinterpret_cast<char*>(&be32_length), 4);
    packet.append(serialized);

    conn->send(packet);
}
#endif

void RpcServer::registerService(RpcServiceAdapter* adapter)
{
    //             CoreX.rpc.MathService
    dispatchTable_[adapter->serviceName()] = adapter;
}

#if ENABLE_TIMESTAMP
void RpcServer::sendErrorReasponse(const TcpConnectionPtr& conn,
                        uint64_t id, CoreX::rpc::ErrorCode code, const std::string& errMsg,
                        uint64_t server_recv_ts)
{
    CoreX::rpc::RpcMessage rpcMsg;
    rpcMsg.set_type(CoreX::rpc::ERROR);
    rpcMsg.set_id(id);
    rpcMsg.set_error(code);
    rpcMsg.set_payload(errMsg);
    rpcMsg.set_server_recv_ts(server_recv_ts);
    rpcMsg.set_server_send_ts(now_us());

    std::string serialized = rpcMsg.SerializeAsString();

    uint32_t length = serialized.size();
    uint32_t magic = 0x42414E41;

    uint32_t be32_length = htonl(length);
    uint32_t be32_magic = htonl(magic);

    std::string packet;
    packet.append(reinterpret_cast<char*>(&be32_magic),4);
    packet.append(reinterpret_cast<char*>(&be32_length),4);
    packet.append(serialized);

    conn->send(packet);
}
#else
void RpcServer::sendErrorReasponse(const TcpConnectionPtr& conn,
                        uint64_t id, CoreX::rpc::ErrorCode code, const std::string& errMsg)
{
    CoreX::rpc::RpcMessage rpcMsg;
    rpcMsg.set_type(CoreX::rpc::ERROR);
    rpcMsg.set_id(id);
    rpcMsg.set_error(code);
    rpcMsg.set_payload(errMsg);

    std::string serialized = rpcMsg.SerializeAsString();

    uint32_t length = serialized.size();
    uint32_t magic = 0x42414E41;

    uint32_t be32_length = htonl(length);
    uint32_t be32_magic = htonl(magic);

    std::string packet;
    packet.append(reinterpret_cast<char*>(&be32_magic),4);
    packet.append(reinterpret_cast<char*>(&be32_length),4);
    packet.append(serialized);

    conn->send(packet);
}
#endif
