#pragma once
#include "../net/EventLoop.hpp"
#include "../net/Channel.hpp"
#include "../net/TcpConnection.hpp"
#include "../net/Buffer.hpp"
#include "../ipc/IpcEndpoint.hpp"
#include <memory>
#include <functional>

// ============================================================================
// IpcRpcBridge — 服务端 IPC→EventLoop 桥接器
//
// 功能：
//   1. 在 EventLoop 上注册 eventfd Channel，监听客户端通知
//   2. 收到通知后从共享内存 RingBuffer 取出请求，包装为 Buffer
//   3. 通过 messageCallback_ 送入 RpcCodec::Onmessage 管线
//   4. 创建虚拟 TcpConnection（基于 socketpair），其 OutputFunc
//      将响应直接推入响应 RingBuffer 并通知客户端
//
// 与 RpcServer 的集成：
//   RpcServer::enableIpc() 创建 IpcRpcBridge，将 messageCallback
//   指向 codec_.Onmessage，与 TCP 路径共享同一套编解码+分发逻辑。
// ============================================================================
class IpcRpcBridge
{
public:
    // messageCallback 签名与 TcpServer::messageCallback 一致
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&, Buffer&)>;

    IpcRpcBridge(EventLoop* loop, const std::string& shmName);
    ~IpcRpcBridge();

    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }

    // 启动：将 eventfd Channel 注册到 EventLoop
    void start();
    // 停止：移除 Channel
    void stop();

    // 用于 RpcServer 关联错误/关闭回调
    std::shared_ptr<TcpConnection> connection() const { return ipcConnection_; }

private:
    // eventfd 可读时调用：排空计数器 + drainRingBuffer
    void onIpcSignal();
    // 从请求环取出所有可用请求并分发
    void drainRingBuffer();

    EventLoop*                    loop_;
    std::unique_ptr<IpcEndpoint>  endpoint_;
    std::unique_ptr<Channel>      signalChannel_;
    std::shared_ptr<TcpConnection>              ipcConnection_;    // 虚拟连接，用于响应回传
    MessageCallback               messageCallback_;
    int                           signalFd_ = -1;    // 本地 eventfd
    int                           closeFd_  = -1;    // socketpair 另一端（析构时关闭）
    bool                          started_  = false;
};
