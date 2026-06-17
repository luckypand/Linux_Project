#include "IpcEndpoint.hpp"
#include <sys/eventfd.h>
#include <unistd.h>
#include <thread>
#include <chrono>

IpcEndpoint::IpcEndpoint(const std::string& shmName, bool isServer)
    : pool_(shmName, IPC_SHM_SIZE,
            isServer ? ShmMemoryPool::CREATE : ShmMemoryPool::ATTACH)
    , isServer_(isServer)
{
    void* base = pool_.GetMappedptr();

    // 共享内存布局:
    // [IpcShmHeader 64B] [ShmRingBuffer requestRing] [ShmRingBuffer responseRing]
    header_       = static_cast<IpcShmHeader*>(base);
    requestRing_  = reinterpret_cast<ShmRingBuffer*>(
                        static_cast<char*>(base) + sizeof(IpcShmHeader));
    responseRing_ = reinterpret_cast<ShmRingBuffer*>(
                        static_cast<char*>(base) + sizeof(IpcShmHeader) + sizeof(ShmRingBuffer));

    // 创建自己的 eventfd (非阻塞 + close-on-exec)
    localSignalFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    if (isServer_) {
        // 服务端：初始化两个 RingBuffer（placement new 在已映射的共享内存上）
        new (requestRing_)  ShmRingBuffer();
        new (responseRing_) ShmRingBuffer();

        // 将自己的 eventfd 写入 header，供客户端读取
        header_->serverSignalFd.store(localSignalFd_, std::memory_order_release);

        // 等待客户端写入其 eventfd
        int clientFd = -1;
        while ((clientFd = header_->clientSignalFd.load(std::memory_order_acquire)) == -1) {
            std::this_thread::yield();
        }
        peerSignalFd_ = clientFd;
    } else {
        // 客户端：将自己的 eventfd 写入 header，供服务端读取
        header_->clientSignalFd.store(localSignalFd_, std::memory_order_release);

        // 等待服务端写入其 eventfd
        int serverFd = -1;
        while ((serverFd = header_->serverSignalFd.load(std::memory_order_acquire)) == -1) {
            std::this_thread::yield();
        }
        peerSignalFd_ = serverFd;
    }
}

IpcEndpoint::~IpcEndpoint()
{
    if (localSignalFd_ >= 0) {
        ::close(localSignalFd_);
    }
}

// ========== 客户端 API ==========

bool IpcEndpoint::sendRequest(const std::string& tlvPacket)
{
    return requestRing_->CAS_Push(tlvPacket.data(), tlvPacket.size());
}

bool IpcEndpoint::recvResponse(std::string& tlvPacket, int timeoutMs)
{
    if (timeoutMs < 0) timeoutMs = 5000;

    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeoutMs);
    do {
        if (responseRing_->CAS_Pop(tlvPacket)) {
            return true;
        }
        std::this_thread::yield();//通过yield让出时间片，使用定时器的做法，可能延迟退出
    } while (std::chrono::steady_clock::now() < deadline);

    return false;
}

void IpcEndpoint::notifyServer()
{
    if (peerSignalFd_ < 0) return;
    uint64_t val = 1;
    ::write(peerSignalFd_, &val, sizeof(val));
}

// ========== 服务端 API ==========

bool IpcEndpoint::tryRecvRequest(std::string& tlvPacket)
{
    return requestRing_->CAS_Pop(tlvPacket);
}

bool IpcEndpoint::sendResponse(const std::string& tlvPacket)
{
    return responseRing_->CAS_Push(tlvPacket.data(), tlvPacket.size());
}

void IpcEndpoint::notifyClient()
{
    if (peerSignalFd_ < 0) return;
    uint64_t val = 1;
    ::write(peerSignalFd_, &val, sizeof(val));
}
