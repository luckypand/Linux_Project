// ============================================================================
// ShmTopicBus.cpp — 共享内存 Topic 总线 实现
// ============================================================================

#include "ShmTopicBus.hpp"
#include "../ipc/ShmSegment.hpp"
#include "RosCompat.hpp"

#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <cctype>
#include <chrono>
#include <thread>
#include <algorithm>

// ============================================================================
// 工具函数
// ============================================================================

static uint64_t nowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string ShmTopicBus::shmNameForTopic(const std::string& rosTopic)
{
    // 将 ROS topic 名转换为合法的 SHM 名
    // "/camera/image_raw" → "corex_topic_camera_image_raw"
    std::string name = "corex_topic";
    for (char c : rosTopic) {
        if (c == '/') {
            name += '_';
        } else if (std::isalnum(c) || c == '_' || c == '-') {
            name += c;
        }
    }
    return name;
}

bool ShmTopicBus::exists(const std::string& topicName)
{
    std::string shmName = shmNameForTopic(topicName);
    // 尝试以 ATTACH 模式打开（只读），成功则存在
    try {
        ShmMemoryPool pool(shmName, 1, ShmMemoryPool::ATTACH);
        return pool.GetMappedptr() != nullptr;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// 槽位访问辅助
// ============================================================================

inline ShmTopicSlot* ShmTopicBus::slotAt(int index)
{
    uint8_t* base = slotsBase_ + index * (sizeof(ShmTopicSlot) + maxMsgSize_);
    return reinterpret_cast<ShmTopicSlot*>(base);
}

inline const ShmTopicSlot* ShmTopicBus::slotAt(int index) const
{
    const uint8_t* base = slotsBase_ + index * (sizeof(ShmTopicSlot) + maxMsgSize_);
    return reinterpret_cast<const ShmTopicSlot*>(base);
}

inline uint8_t* ShmTopicBus::slotData(int index)
{
    return reinterpret_cast<uint8_t*>(slotAt(index)) + sizeof(ShmTopicSlot);
}

inline const uint8_t* ShmTopicBus::slotData(int index) const
{
    return reinterpret_cast<const uint8_t*>(slotAt(index)) + sizeof(ShmTopicSlot);
}

// ============================================================================
// 构造 / 析构
// ============================================================================
ShmTopicBus::ShmTopicBus(const std::string& topicName, size_t maxMsgSize,
                         int slotCount, bool isCreator)
    : topicName_(topicName)
    , maxMsgSize_(maxMsgSize)
    , slotCount_(std::min(slotCount, static_cast<int>(SHM_TOPIC_MAX_SLOTS)))
    , isCreator_(isCreator)
{
    if (slotCount_ < 1) slotCount_ = 1;

    std::string shmName  = shmNameForTopic(topicName_);
    size_t      totalSz  = ShmTopicTotalSize(maxMsgSize_, slotCount_);

    try {
        pool_ = std::make_unique<ShmMemoryPool>(
            shmName, totalSz,
            isCreator_ ? ShmMemoryPool::CREATE : ShmMemoryPool::ATTACH);
    } catch (const std::exception& e) {
        BRIDGE_ROS_ERROR("[ShmTopicBus] Failed to open SHM '%s': %s",
                         shmName.c_str(), e.what());
        meta_ = nullptr;
        slotsBase_ = nullptr;
        return;
    }

    void* raw = pool_->GetMappedptr();
    if (!raw) {
        meta_ = nullptr;
        slotsBase_ = nullptr;
        return;
    }

    uint8_t* base = static_cast<uint8_t*>(raw);
    meta_      = reinterpret_cast<ShmTopicMeta*>(base);
    slotsBase_ = base + sizeof(ShmTopicMeta);

    // 创建者：初始化元数据
    if (isCreator_) {
        memset(meta_, 0, sizeof(ShmTopicMeta));
        meta_->magic        = SHM_TOPIC_MAGIC;
        meta_->max_msg_size = static_cast<uint32_t>(maxMsgSize_);
        meta_->slot_count   = slotCount_;
        strncpy(meta_->topic_name, topicName_.c_str(), sizeof(meta_->topic_name) - 1);
        meta_->write_seq.store(0, std::memory_order_relaxed);

        // 初始化订阅者列表
        for (int i = 0; i < SHM_TOPIC_MAX_SUBSCRIBERS; i++) {
            meta_->subscriber_fds[i] = -1;
        }

        // 初始化所有槽位的 seq_num = 0 (空闲)
        for (int i = 0; i < slotCount_; i++) {
            slotAt(i)->seq_num.store(0, std::memory_order_relaxed);
            slotAt(i)->msg_size = 0;
        }

        BRIDGE_ROS_INFO("[ShmTopicBus] Created topic '%s' SHM=%s slots=%d msgSize=%zuB total=%zuB",
                        topicName_.c_str(), shmName.c_str(), slotCount_, maxMsgSize_, totalSz);
    } else {
        // 附加者：校验魔数
        if (meta_->magic != SHM_TOPIC_MAGIC) {
            BRIDGE_ROS_ERROR("[ShmTopicBus] Magic mismatch for '%s': expected 0x%X got 0x%X",
                             topicName_.c_str(), SHM_TOPIC_MAGIC, meta_->magic);
            meta_ = nullptr;
            return;
        }
        BRIDGE_ROS_INFO("[ShmTopicBus] Attached to topic '%s' SHM=%s",
                        topicName_.c_str(), shmName.c_str());
    }
}

ShmTopicBus::~ShmTopicBus()
{
    // 取消订阅
    if (localEventFd_ >= 0) {
        unsubscribe(localEventFd_);
        ::close(localEventFd_);
        localEventFd_ = -1;
    }
    // ShmMemoryPool 析构时自动 unmap
}

// ============================================================================
// 发布
// ============================================================================
bool ShmTopicBus::publish(const void* data, size_t size)
{
    if (!meta_ || !slotsBase_) return false;
    if (size > maxMsgSize_) {
        BRIDGE_ROS_ERROR("[ShmTopicBus] Message too large: %zu > %zu (topic=%s)",
                         size, maxMsgSize_, topicName_.c_str());
        return false;
    }

    // 1. 获取下一个写入序号和槽位索引
    uint64_t seq = meta_->write_seq.fetch_add(1, std::memory_order_acquire);
    int idx = static_cast<int>(seq % slotCount_);

    ShmTopicSlot* slot = slotAt(idx);
    uint8_t*      dst  = slotData(idx);

    // 2. 写入消息数据（在设置 seq_num 之前完成，保证 reader 看到 seq_num 时数据已就绪）
    memcpy(dst, data, size);
    slot->msg_size = static_cast<uint32_t>(size);

    // 3. 发布 (release 屏障: 确保数据写入对 reader 可见)
    slot->seq_num.store(seq + 1, std::memory_order_release);  // +1 避免 seq=0 和初始值冲突

    // 4. 通知所有订阅者
    int count = meta_->subscriber_count;
    for (int i = 0; i < SHM_TOPIC_MAX_SUBSCRIBERS && count > 0; i++) {
        int fd = meta_->subscriber_fds[i];
        if (fd >= 0) {
            uint64_t val = 1;
            ::write(fd, &val, sizeof(val));  // 唤醒订阅者 (best-effort)
            count--;
        }
    }

    return true;
}

// ============================================================================
// 订阅
// ============================================================================
int ShmTopicBus::subscribe()
{
    if (!meta_) return -1;

    std::lock_guard<std::mutex> lock(subMutex_);

    // 创建本地 eventfd
    int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd < 0) return -1;

    // 在 SHM 的订阅者列表中注册
    bool registered = false;
    for (int i = 0; i < SHM_TOPIC_MAX_SUBSCRIBERS; i++) {
        int expected = -1;
        // 原子 CAS 抢占槽位（需要跨进程可见，用原子比较交换）
        // 简化: 直接赋值（发布者只读，订阅者写入受 subMutex_ 保护）
        if (meta_->subscriber_fds[i] == -1) {
            meta_->subscriber_fds[i] = efd;
            meta_->subscriber_count++;
            registered = true;
            break;
        }
    }

    if (!registered) {
        ::close(efd);
        BRIDGE_ROS_ERROR("[ShmTopicBus] Subscriber slots full for '%s' (max=%zu)",
                         topicName_.c_str(), (size_t)SHM_TOPIC_MAX_SUBSCRIBERS);
        return -1;
    }

    localEventFd_ = efd;
    lastReadSeq_  = 0;  // 从头开始读

    BRIDGE_ROS_INFO("[ShmTopicBus] Subscribed to '%s' fd=%d count=%d",
                    topicName_.c_str(), efd, meta_->subscriber_count);

    return efd;
}

// ============================================================================
// 取消订阅
// ============================================================================
void ShmTopicBus::unsubscribe(int fd)
{
    if (!meta_ || fd < 0) return;

    std::lock_guard<std::mutex> lock(subMutex_);

    for (int i = 0; i < SHM_TOPIC_MAX_SUBSCRIBERS; i++) {
        if (meta_->subscriber_fds[i] == fd) {
            meta_->subscriber_fds[i] = -1;
            meta_->subscriber_count--;
            BRIDGE_ROS_INFO("[ShmTopicBus] Unsubscribed from '%s' fd=%d count=%d",
                            topicName_.c_str(), fd, meta_->subscriber_count);
            break;
        }
    }

    if (fd == localEventFd_) {
        localEventFd_ = -1;
    }
}

// ============================================================================
// 非阻塞读取
// ============================================================================
bool ShmTopicBus::tryRecv(std::string& outMsg)
{
    if (!meta_ || !slotsBase_) return false;

    // 遍历所有槽位，找最新的有效消息
    uint64_t maxSeq    = 0;
    int      maxSeqIdx = -1;

    for (int i = 0; i < slotCount_; i++) {
        uint64_t seq = slotAt(i)->seq_num.load(std::memory_order_acquire);
        if (seq > 0 && seq > maxSeq) {
            maxSeq    = seq;
            maxSeqIdx = i;
        }
    }

    // 没有有效消息，或没有比上次更新的消息
    if (maxSeqIdx < 0 || maxSeq <= lastReadSeq_) {
        return false;
    }

    // 读取数据
    const ShmTopicSlot* slot = slotAt(maxSeqIdx);
    uint32_t msgSize = slot->msg_size;

    if (msgSize > 0 && msgSize <= maxMsgSize_) {
        outMsg.assign(reinterpret_cast<const char*>(slotData(maxSeqIdx)), msgSize);
    }

    lastReadSeq_ = maxSeq;

    // 排空 eventfd（如果有本地 eventfd）
    if (localEventFd_ >= 0) {
        uint64_t val;
        ::read(localEventFd_, &val, sizeof(val));  // 非阻塞, 清空计数器
    }

    return true;
}

// ============================================================================
// 阻塞等待读取
// ============================================================================
bool ShmTopicBus::recvWait(std::string& outMsg, int timeoutMs)
{
    if (!meta_) return false;

    // 先尝试非阻塞读
    if (tryRecv(outMsg)) return true;

    // 如果没有本地 eventfd，无法等待
    if (localEventFd_ < 0) {
        BRIDGE_ROS_WARN("[ShmTopicBus] recvWait called without subscription");
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);

    while (true) {
        // 等待 eventfd（1ms 轮询）
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();

        if (remaining <= 0) {
            // 超时前最后尝试一次
            return tryRecv(outMsg);
        }

        // 使用 poll 等待 eventfd（比 sleep 更精确）
        struct pollfd pfd;
        pfd.fd      = localEventFd_;
        pfd.events  = POLLIN;
        pfd.revents = 0;

        int pollTimeout = static_cast<int>(std::min(remaining, 100L));
        int ret = ::poll(&pfd, 1, pollTimeout);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            uint64_t val;
            ::read(localEventFd_, &val, sizeof(val));  // 排空
            if (tryRecv(outMsg)) return true;
        } else if (ret < 0 && errno != EINTR) {
            return false;
        }
        // ret == 0 (timeout): 循环重试
    }
}

// ============================================================================
// 查询
// ============================================================================
int ShmTopicBus::subscriberCount() const
{
    if (!meta_) return 0;
    return meta_->subscriber_count;
}

uint64_t ShmTopicBus::lastWriteSeq() const
{
    if (!meta_) return 0;
    return meta_->write_seq.load(std::memory_order_relaxed);
}

void ShmTopicBus::drainEventfd(int fd)
{
    if (fd < 0) return;
    uint64_t val;
    ::read(fd, &val, sizeof(val));
}
