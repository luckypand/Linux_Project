#pragma once
#include "../net/TcpConnection.hpp"
#include "../net/Buffer.hpp"

constexpr uint32_t RPC_MAGIC_NUMBER = 0x42414E41;  //自定义魔数 "BANA"，8位16进制
constexpr size_t HEADER_SIZE = 8;   //4字节魔数 + 4字节包长度
constexpr size_t MAX_SINGLEDATA = size_t(64) * 1024 * 1024;

class RpcCodec
{
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
    using ProtobuffMessageCallback = std::function<void(const TcpConnectionPtr&,const std::string&)>;
    RpcCodec() {}
    void setbusinessCallback_(ProtobuffMessageCallback cb) { businessCallback_ = std::move(cb); }
    void Onmessage(const TcpConnectionPtr& conn,Buffer& buf);
private:
    ProtobuffMessageCallback businessCallback_;
};