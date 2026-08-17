#pragma once
#include <cstdint>
#include <string>
#include <chrono>
#include "QosPolicy.hpp"
#include "ShmDataBlock.hpp"
#include "SubscriberRegistry.hpp"
#include "HistoryCache.hpp"

class BufferPool;

// ============================================================
// TopicChannel — 单 Topic 协调中枢
// ============================================================
//
// 共享内存布局：
// ┌──────────────────────────────┐
// │ TopicChannelHeader (64B)      │  topic_id / qos / 元数据
// ├──────────────────────────────┤
// │ RegistryHeader + Slots...     │  SubscriberRegistry 数据
// └──────────────────────────────┘
//
// 职责：
//   生产者侧：publish(data,size) → allocate → commit → pushToAll → history
//   消费者侧：subscribe() → receive() → heartbeat
//   生命周期：超时扫描 + 异常回收

struct alignas(64) TopicChannelHeader
{
    uint32_t topic_id;
    QoSType  qos_type;
    uint32_t qos_depth;
    uint32_t max_subscribers;
    uint32_t ring_capacity;
    uint32_t _padding[2];  // 对齐到 64B

    static constexpr size_t requiredSize(uint32_t max_subs, uint32_t ring_cap) {
        return sizeof(TopicChannelHeader) + RegistryHeader::requiredSize(max_subs, ring_cap);
    }
};
static_assert(sizeof(TopicChannelHeader) == 64, "TopicChannelHeader must be 64 bytes");

// ============================================================
class TopicChannel
{
public:
    TopicChannel() = default;
    ~TopicChannel() = default;

    // === 初始化 ===
    // shm_base: 本 Channel 在共享内存中的起始地址（已分配 requiredSize 字节）
    // topic_id: Topic 编号
    // qos:      QoS 策略
    // pool:     共享 BufferPool（不拥有所有权）
    void init(void* shm_base, uint32_t topic_id, const QosPolicy& qos,
              BufferPool* pool, uint32_t max_subs, uint32_t ring_cap);

    // 附加已有 Channel（消费者侧）
    void attach(void* shm_base, BufferPool* pool);

    // === 生产者 API ===
    // 发布数据：返回 block_id（可用于追踪），失败返回 UINT32_MAX
    uint32_t publish(const void* data, uint32_t size);

    // === 消费者 API ===
    int32_t subscribe();                                   // 注册，返回 sub_id
    void    unsubscribe(uint32_t sub_id);                   // 注销
    bool    receive(uint32_t sub_id, void* out_buf, uint32_t& in_out_size);  // 接收一条
    void    heartbeat(uint32_t sub_id, uint64_t now_us);   // 更新心跳
    std::vector<uint32_t> checkTimeout(uint64_t now_us, uint64_t timeout_us);

    // === 查询 ===
    uint32_t topicId()        const { return header_->topic_id; }
    QoSType  qosType()        const { return header_->qos_type; }
    uint32_t subscriberCount() const { return registry_.activeCount(); }

private:
    TopicChannelHeader* header_;
    BufferPool*         pool_;       // 不拥有
    SubscriberRegistry  registry_;   // 管理共享内存中的订阅者
    HistoryCache        history_;    // 进程本地（生产者写入）
};
