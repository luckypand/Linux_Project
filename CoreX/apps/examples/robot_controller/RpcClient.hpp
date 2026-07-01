// ============================================================================
// RpcClient.hpp — 轻量级 CoreX RPC 客户端封装
//
// 封装 TCP socket + TLV 帧协议 + protobuf 序列化，提供同步 RPC 调用接口。
// 不依赖 Reactor 网络库，可直接链接 librpc.a（仅用其 proto 定义）。
//
// 使用方式：
//   RpcClient client("192.168.1.100", 8080);
//   if (!client.connect()) { ... }
//
//   OdometryRequest  req;
//   OdometryResponse resp;
//   if (client.call("RobotTelemetry", "GetOdometry", req, resp)) {
//       cout << "vel_x = " << resp.vel_x() << endl;
//   }
// ============================================================================

#pragma once

#include "../../../proto/rpc_message.pb.h"
#include "../../../proto/robot_service.pb.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <chrono>
#include <iostream>

class RpcClient
{
public:
    static constexpr uint32_t MAGIC       = 0x42414E41;
    static constexpr size_t   HEADER_SIZE = 8;
    static constexpr size_t   MAX_PAYLOAD = 64 * 1024 * 1024;  // 64 MB

    RpcClient(const std::string& host, uint16_t port)
        : host_(host), port_(port), sock_(-1)
    {}

    ~RpcClient() { disconnect(); }

    // ---- 连接 / 断开 ----
    bool connect(int timeoutSec = 5)
    {
        sock_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (sock_ < 0) return false;

        int one = 1;
        ::setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        // 设置连接超时
        struct timeval tv = {timeoutSec, 0};
        ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port_);
        if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
            ::close(sock_); sock_ = -1; return false;
        }

        if (::connect(sock_, reinterpret_cast<sockaddr*>(&addr),
                      sizeof(addr)) < 0) {
            ::close(sock_); sock_ = -1; return false;
        }

        msgId_ = 0;
        return true;
    }

    void disconnect()
    {
        if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
    }

    bool isConnected() const { return sock_ >= 0; }

    // ---- 同步 RPC 调用 ----
    // 模板参数 Req/Resp 需为 protobuf Message 子类
    template <typename Req, typename Resp>
    bool call(const std::string& service, const std::string& method,
              const Req& request, Resp& response,
              double timeoutSec = 5.0)
    {
        if (sock_ < 0) return false;

        try {
            // 1. 序列化内部请求
            std::string innerPayload;
            if (!request.SerializeToString(&innerPayload)) return false;

            // 2. 构建 RpcMessage 信封
            CoreX::rpc::RpcMessage rpcMsg;
            rpcMsg.set_type(CoreX::rpc::REQUEST);
            rpcMsg.set_id(++msgId_);
            rpcMsg.set_service(service);
            rpcMsg.set_method(method);
            rpcMsg.set_payload(innerPayload);
            rpcMsg.set_client_send_ts(nowUs());

            std::string wirePayload;
            if (!rpcMsg.SerializeToString(&wirePayload)) return false;

            // 3. 添加 TLV 头
            std::string packet = buildPacket(wirePayload);

            // 4. 发送
            sendAll(packet.data(), packet.size());

            // 5. 接收响应头
            uint8_t header[HEADER_SIZE];
            recvAll(header, HEADER_SIZE);

            // 6. 校验魔数
            uint32_t magic;
            memcpy(&magic, header, 4);
            if (ntohl(magic) != MAGIC) return false;

            // 7. 读取 payload
            uint32_t payloadLen;
            memcpy(&payloadLen, header + 4, 4);
            payloadLen = ntohl(payloadLen);

            if (payloadLen == 0 || payloadLen > MAX_PAYLOAD) return false;

            std::string respPayload(payloadLen, '\0');
            recvAll(&respPayload[0], payloadLen);

            // 8. 解析 RpcMessage
            CoreX::rpc::RpcMessage rpcResp;
            if (!rpcResp.ParseFromString(respPayload)) return false;
            if (rpcResp.type() != CoreX::rpc::RESPONSE) return false;

            // 9. 解析业务响应
            return response.ParseFromString(rpcResp.payload());

        } catch (const std::exception& e) {
            std::cerr << "[RpcClient] call failed: " << e.what() << std::endl;
            disconnect();
            return false;
        }
    }

private:
    std::string buildPacket(const std::string& payload)
    {
        uint32_t beMagic = htonl(MAGIC);
        uint32_t beLen   = htonl(static_cast<uint32_t>(payload.size()));

        std::string packet;
        packet.reserve(HEADER_SIZE + payload.size());
        packet.append(reinterpret_cast<const char*>(&beMagic), 4);
        packet.append(reinterpret_cast<const char*>(&beLen), 4);
        packet.append(payload);
        return packet;
    }

    void sendAll(const void* data, size_t len)
    {
        size_t remaining = len;
        auto*  pos = static_cast<const char*>(data);
        while (remaining > 0) {
            ssize_t w = ::send(sock_, pos, remaining, MSG_NOSIGNAL);
            if (w < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(std::string("send: ") + strerror(errno));
            }
            remaining -= w;
            pos += w;
        }
    }

    void recvAll(void* buf, size_t len)
    {
        size_t remaining = len;
        auto*  pos = static_cast<char*>(buf);
        while (remaining > 0) {
            ssize_t r = ::recv(sock_, pos, remaining, MSG_WAITALL);
            if (r == 0) throw std::runtime_error("connection closed");
            if (r < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(std::string("recv: ") + strerror(errno));
            }
            remaining -= r;
            pos += r;
        }
    }

    static uint64_t nowUs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::string host_;
    uint16_t    port_;
    int         sock_;
    uint64_t    msgId_ = 0;
};
