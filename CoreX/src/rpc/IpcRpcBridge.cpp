#include "IpcRpcBridge.hpp"
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

IpcRpcBridge::IpcRpcBridge(EventLoop* loop, const std::string& shmName)
    : loop_(loop)
    , endpoint_(std::make_unique<IpcEndpoint>(shmName, true))  // isServer = true → CREATE
{
    signalFd_ = endpoint_->serverSignalFd();

    // 创建 socketpair 作为虚拟连接的"内核"
    // fds[0] 给 TcpConnection 持有（Socket RAII 会在析构时关闭它）
    // fds[1] 由 IpcRpcBridge 持有（防止对端 HUP），析构时关闭
    int fds[2];
    int ret = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    (void)ret;  // 在正常环境下 socketpair 不会失败

    ipcConnection_ = std::make_shared<TcpConnection>(loop_, fds[0]);

    // 响应回传路径：不经过 socket，直接推入响应 RingBuffer
    ipcConnection_->setOutputFunc(
        [this](const std::string& data) {
            // 自旋直到推入成功（RingBuffer 在 4096 字节 block 下极少满）
            while (!endpoint_->sendResponse(data)) {
                std::this_thread::yield();
            }
            endpoint_->notifyClient();
        }
    );

    // 防止 socketpair 关闭时触发 handleClose 导致空回调崩溃
    ipcConnection_->setCloseCallback([](const std::shared_ptr<TcpConnection>&) {});

    closeFd_ = fds[1];
}

IpcRpcBridge::~IpcRpcBridge()
{
    stop();
    if (closeFd_ >= 0) {
        ::close(closeFd_);
    }
}

void IpcRpcBridge::start()
{
    if (started_) return;
    started_ = true;

    // 在 EventLoop 线程上执行初始化
    loop_->RunInloop([this]() {
        // 建立虚拟连接（状态 → kConnected，但不需要读 socket 数据）
        ipcConnection_->connectEstablished();
        ipcConnection_->disableReadEvent();

        // 注册 eventfd Channel 到 epoll
        signalChannel_ = std::make_unique<Channel>(loop_, signalFd_);
        signalChannel_->setReadCallback([this]() { onIpcSignal(); });
        signalChannel_->enableReading();
    });
}

void IpcRpcBridge::stop()
{
    if (!started_) return;
    started_ = false;

    // 必须在 EventLoop 线程上操作
    if (loop_->IsInloopthread()) {
        signalChannel_->disableall();
        loop_->RemoveChannel(signalChannel_.get());
        signalChannel_.reset();
    } else {
        loop_->RunInloop([this]() {
            signalChannel_->disableall();
            loop_->RemoveChannel(signalChannel_.get());
            signalChannel_.reset();
        });
    }
}

void IpcRpcBridge::onIpcSignal()
{
    // 排空 eventfd 计数器（累加的信号一次清空）
    uint64_t val;
    ssize_t nr = ::read(signalFd_, &val, sizeof(val));
    (void)nr;  // eventfd read 在正常流程中不会失败

    // 处理所有可用的请求
    drainRingBuffer();
}

void IpcRpcBridge::drainRingBuffer()
{
    std::string tlvPacket;
    while (endpoint_->tryRecvRequest(tlvPacket)) {
        // 包装为 Buffer，送入与 TCP 路径相同的编解码管线
        Buffer buf;
        buf.append(tlvPacket.data(), tlvPacket.size());

        if (messageCallback_) {
            messageCallback_(ipcConnection_, buf);
        }
    }
}
