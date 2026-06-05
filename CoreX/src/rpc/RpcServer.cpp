#include "RpcServer.hpp"
#include <arpa/inet.h>

RpcServer::RpcServer(EventLoop* loop,const std::string& ip,uint16_t port,const std::string& name)
    :server_(loop,ip,port,name)
{
    codec_.setbusinessCallback_(
        [this](const TcpConnectionPtr& conn,const std::string& payload){
            this->handleRpcCodecMessage(conn,payload);
        }//传递实际路由希望做的
    );
    server_.setThreadNum(4);
    server_.setMessageCallback(  //服务端收到客户端请求(这时应该是序列化后的rpc请求)，需要反序列化后路由处理
        [this](const TcpConnectionPtr& conn,Buffer& buf){
            codec_.Onmessage(conn,buf);
        }
    );
}

/*
* @brief:
*     此处为RpcCodec进行解码后businessCallback_实际执行的逻辑,即进行
解包获得元数据rpcMsg，dispatch进行具体的逻辑处理，最终返回序列化处理后的结果
  @param:
        conn    本次接收连接的Tcpconnection
        payload	接收到的序列化后的完整Protobuf字节流
*/
void RpcServer::handleRpcCodecMessage(const TcpConnectionPtr& conn,const std::string& payload)
{
    CoreX::rpc::RpcMessage rpcMsg;
    if(!rpcMsg.ParseFromString(payload))
    {
        sendErrorReasponse(conn,0,
                            CoreX::rpc::WRONG_PROTO
                            ,"Fail to parse payload");
        return; //解析失败
    }

    if(rpcMsg.type() != CoreX::rpc::REQUEST)
    {
        return;
    }

    auto it = dispatchTable_.find(rpcMsg.service());
    if(it != dispatchTable_.end())
    {
        std::string result = it->second->dispatch(rpcMsg.method(),rpcMsg.payload());
        if(!result.empty())
        {
            sendResponse(conn,rpcMsg.id(),result);
        }
    }
}

// 8 字节 TLV 封包与发送机制
void RpcServer::sendResponse(const TcpConnectionPtr& conn, uint64_t id,const std::string& rstPayload) 
{
    //将结果进行封装
    CoreX::rpc::RpcMessage rpcMsg;
    rpcMsg.set_type(CoreX::rpc::RESPONSE);
    rpcMsg.set_id(id);
    rpcMsg.set_payload(rstPayload);

    std::string serialized = rpcMsg.SerializeAsString(); //?
    
    uint32_t length = serialized.size();
    uint32_t magic = 0x42414E41; // 魔数 "BANA"

    // 转换为网络字节序
    uint32_t be32_magic = htonl(magic);
    uint32_t be32_length = htonl(length);

    // 组装包头与包体
    std::string packet;
    packet.append(reinterpret_cast<char*>(&be32_magic), 4);
    packet.append(reinterpret_cast<char*>(&be32_length), 4);
    packet.append(serialized); 

    // 异步非阻塞发送
    conn->send(packet);
}

void RpcServer::registerService(RpcServiceAdapter* adapter)
{
    //             CoreX.rpc.MathService
    dispatchTable_[adapter->serviceName()] = adapter;
}

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