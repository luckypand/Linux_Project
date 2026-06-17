#pragma once
#include "ShmSegment.hpp"
#include "RingBuffer.hpp"
#include <string>
#include <atomic>
#include <cstddef>

// 共享内存段头部的控制信息（放在 mmap 区域最前面，cacheline 对齐）
struct IpcShmHeader {
    std::atomic<int> serverSignalFd{-1};  // eventfd：客户端写 → 服务端被唤醒
    std::atomic<int> clientSignalFd{-1};  // eventfd：服务端写 → 客户端被唤醒
    char             pad[56];             // 填充至 64 字节 cacheline 边界
};
static_assert(sizeof(IpcShmHeader) == 64, "IpcShmHeader must be 64 bytes (cacheline-aligned)");

// 共享内存总大小 = header + 请求环 + 响应环
constexpr size_t IPC_SHM_SIZE = sizeof(IpcShmHeader) + 2 * sizeof(ShmRingBuffer);

// ============================================================================
// IpcEndpoint — 双向共享内存 IPC 端点（客户端 ↔ 服务端）
//
// 使用方式：
//   服务端: IpcEndpoint ep("/corex_rpc", true);   // CREATE 模式
//   客户端: IpcEndpoint ep("/corex_rpc", false);  // ATTACH 模式
//
//   // 客户端发送请求：
//   ep.sendRequest(tlvPacket);
//   ep.notifyServer();
//   std::string resp;
//   ep.recvResponse(resp, 5000);
//
//   // 服务端接收并回复：
//   std::string req;
//   if (ep.tryRecvRequest(req)) { ... }
//   ep.sendResponse(tlvRespPacket);
//   ep.notifyClient();
// ============================================================================
class IpcEndpoint
{
public:
    // isServer=true → CREATE 模式（创建并初始化共享内存）
    // isServer=false → ATTACH 模式（连接到已有共享内存）
    IpcEndpoint(const std::string& shmName, bool isServer);
    ~IpcEndpoint();

    // ---- 客户端 API ----
    // 将完整 TLV 包推入请求环（失败表示环满或数据过大，调用方应重试）
    bool sendRequest(const std::string& tlvPacket);
    // 从响应环取回 TLV 包（自旋等待 + yield，timeoutMs < 0 表示默认 5000ms）
    bool recvResponse(std::string& tlvPacket, int timeoutMs = -1);
    // 通知服务端有新请求（写 eventfd）
    void notifyServer();

    // ---- 服务端 API ----
    // 非阻塞地从请求环取一个 TLV 包
    bool tryRecvRequest(std::string& tlvPacket);
    // 将 TLV 响应包推入响应环
    bool sendResponse(const std::string& tlvPacket);
    // 通知客户端有响应到达（写 eventfd）
    void notifyClient();

    // ---- EventLoop 集成（服务端） ----
    // 返回服务端本地 eventfd，可注册到 epoll 等待客户端通知
    int serverSignalFd() const { return localSignalFd_; }

private:
    ShmMemoryPool   pool_;           // 共享内存 RAII 封装
    IpcShmHeader*   header_ = nullptr;
    ShmRingBuffer*  requestRing_ = nullptr;
    ShmRingBuffer*  responseRing_ = nullptr;
    bool            isServer_;

    int localSignalFd_ = -1;         // 自己的 eventfd
    int peerSignalFd_  = -1;         // 对方的 eventfd（从 header 读出）
};
