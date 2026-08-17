#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include "QosPolicy.hpp"
#include "TopicChannel.hpp"

class BufferPool;

// ============================================================
// TopicManager — 全局 Pub/Sub 管理器
// ============================================================
//
// 管理两个 SHM 段：
//   1. BufferPool 段（"/corex_pubsub_pool"）：数据块池
//   2. PubSub 段（"/corex_pubsub"）：Topic 槽位 + 元数据
//
// 布局（PubSub 段）：
// ┌─────────────────────────────┐
// │ PubSubSegmentHeader (64B)    │  magic="PSUB", version, max_topics...
// ├─────────────────────────────┤
// │ TopicSlot[0]                 │  name[64] + TopicChannelHeader + Registry
// │ TopicSlot[1]                 │
// │ ... max_topics 个            │
// └─────────────────────────────┘

constexpr uint32_t PUBSUB_MAGIC   = 0x50535542;  // "PSUB"
constexpr uint32_t PUBSUB_VERSION = 1;
constexpr uint32_t MAX_TOPIC_NAME = 64;

struct alignas(64) PubSubSegmentHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t max_topics;
    uint32_t topic_count;          // 当前已创建的 Topic 数量
    uint32_t max_subscribers;
    uint32_t ring_capacity;
    uint32_t topic_slot_size;      // 每个槽位的字节数
    uint32_t _pad;

    static constexpr size_t requiredSize(uint32_t max_topics, uint32_t slot_size) {
        return sizeof(PubSubSegmentHeader) + max_topics * slot_size;
    }
};
static_assert(sizeof(PubSubSegmentHeader) == 64, "PubSubSegmentHeader must be 64 bytes");

// ============================================================
class TopicManager
{
public:
    // === 创建者：新建两个 SHM 段 ===
    // pubsub_name:   PubSub 段名称（如 "/corex_pubsub"）
    // pool_name:     BufferPool 段名称（如 "/corex_pubsub_pool"）
    // max_topics:    最大 Topic 数量
    // block_count:   每个 Topic 的数据块池大小（暂用全局共享池）
    // max_payload:   每个 Block 最大 payload
    // max_subs:      每个 Topic 最大订阅者数
    // ring_cap:      每个订阅者 Ring 容量（2 的幂）
    TopicManager(const std::string& pubsub_name, const std::string& pool_name,
                 uint32_t max_topics, uint32_t block_count, uint32_t max_payload,
                 uint32_t max_subs, uint32_t ring_cap);

    // === 附加者：打开已有段 ===
    explicit TopicManager(const std::string& pubsub_name, const std::string& pool_name);
    ~TopicManager();

    TopicManager(const TopicManager&) = delete;
    TopicManager& operator=(const TopicManager&) = delete;

    // === Topic 管理 ===
    int32_t       createTopic(const std::string& name, const QosPolicy& qos);
    TopicChannel* getChannel(uint32_t topic_id);
    int32_t       findTopic(const std::string& name) const;

    // === 快捷操作（委托给 TopicChannel）===
    uint32_t publish(uint32_t topic_id, const void* data, uint32_t size);
    int32_t  subscribe(uint32_t topic_id);
    void     unsubscribe(uint32_t topic_id, uint32_t sub_id);
    bool     receive(uint32_t topic_id, uint32_t sub_id, void* buf, uint32_t& size);
    void     heartbeat(uint32_t topic_id, uint32_t sub_id, uint64_t now_us);

    // === 心跳恢复 ===
    void startHeartbeatThread(uint64_t interval_us = 500000, uint64_t timeout_us = 2000000);
    void stopHeartbeatThread();
    void recoverOnce(uint64_t timeout_us);  // 手动触发一次超时回收

    // === 查询 ===
    BufferPool& pool()       { return *pool_; }
    uint32_t    topicCount() const { return header_->topic_count; }
    uint32_t    maxTopics()  const { return header_->max_topics; }
    bool        isValid()    const { return header_ && header_->magic == PUBSUB_MAGIC; }

private:
    // SHM 管理
    std::string pubsub_name_;
    std::string pool_name_;
    bool        is_creator_;
    int         pubsub_fd_;
    size_t      pubsub_size_;
    void*       pubsub_mapped_;

    PubSubSegmentHeader* header_;
    std::unique_ptr<BufferPool> pool_;

    // TopicChannel 缓存（进程本地，延迟创建）
    std::vector<std::unique_ptr<TopicChannel>> channels_;

    // 心跳线程
    std::atomic<bool> hb_running_{false};
    std::thread        hb_thread_;

    // 内部辅助
    uint8_t*       slotBase(uint32_t idx);
    const uint8_t* slotBase(uint32_t idx) const;
    void           scanRecover(uint64_t timeout_us);
    void     initShmCreator(const std::string& ps_name, const std::string& pool_name,
                            uint32_t max_topics, uint32_t block_count, uint32_t max_payload,
                            uint32_t max_subs, uint32_t ring_cap);
    void     initShmAttacher(const std::string& ps_name, const std::string& pool_name);
};
