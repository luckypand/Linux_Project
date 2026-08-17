#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include "BlockIdRing.hpp"

// ============================================================
// SubscriberRegistry — 订阅者管理器
// ============================================================
//
// 共享内存布局：
// ┌─────────────────────────────────────┐
// │ RegistryHeader (64B, alignas(64))    │
// ├─────────────────────────────────────┤
// │ Slot[0]: SubscriberSlot + Ring + buf │
// │ Slot[1]: ...                         │
// └─────────────────────────────────────┘

enum class SubState : uint8_t
{
    INACTIVE = 0,
    ACTIVE   = 1,
    TIMEOUT  = 2
};

struct alignas(64) SubscriberSlot
{
    std::atomic<SubState>   state{SubState::INACTIVE};
    std::atomic<uint64_t>   heartbeat{0};
    uint32_t                subscriber_id;
    uint32_t                _padding;

    BlockIdRing*       ring()       { return reinterpret_cast<BlockIdRing*>(this + 1); }
    const BlockIdRing* ring() const { return reinterpret_cast<const BlockIdRing*>(this + 1); }

    static constexpr size_t slotSize(uint32_t ring_cap) {
        return sizeof(SubscriberSlot) + BlockIdRing::requiredSize(ring_cap);
    }
};
static_assert(sizeof(SubscriberSlot) == 64, "SubscriberSlot must be 64 bytes");

struct alignas(64) RegistryHeader
{
    std::atomic<uint32_t> active_count{0};
    uint32_t              max_slots;
    uint32_t              ring_capacity;
    std::atomic<uint32_t> next_sub_id{0};

    static constexpr size_t requiredSize(uint32_t max_slots, uint32_t ring_cap) {
        return sizeof(RegistryHeader) + max_slots * SubscriberSlot::slotSize(ring_cap);
    }
};

// ============================================================
class SubscriberRegistry
{
public:
    SubscriberRegistry() = default;
    ~SubscriberRegistry() = default;

    void init(void* shm_base, uint32_t max_slots, uint32_t ring_cap);
    // 附加已有 Registry（不重置状态，消费者侧使用）
    void attach(void* shm_base);

    // 注册：返回 >=0 的 sub_id，-1 表示槽位满
    int32_t addSubscriber();

    // 注销：排空队列，所有积压 Block ID 输出到 out_blocks
    void removeSubscriber(uint32_t sub_id, std::vector<uint32_t>& out_blocks);

    // 向所有 ACTIVE 订阅者推送 Block ID
    void pushToAll(uint32_t block_id, std::vector<uint32_t>& out_overwritten);

    // 向指定订阅者推送 Block ID（历史重放用）
    uint32_t pushToOne(uint32_t sub_id, uint32_t block_id);

    // 为指定订阅者取出一个 Block ID
    bool popFor(uint32_t sub_id, uint32_t& out_block_id);

    // 心跳
    void heartbeat(uint32_t sub_id, uint64_t now_us);

    // 扫描超时订阅者 → TIMEOUT，返回超时的 sub_id 列表
    std::vector<uint32_t> checkTimeout(uint64_t now_us, uint64_t timeout_us);

    uint32_t activeCount() const;
    uint32_t maxSlots()    const { return header_->max_slots; }

private:
    RegistryHeader* header_;
    uint8_t*        base_;       // SHM 基地址
    uint32_t        ring_cap_;

    SubscriberSlot* slotByIndex(uint32_t idx) {
        return reinterpret_cast<SubscriberSlot*>(
            base_ + sizeof(RegistryHeader) + idx * SubscriberSlot::slotSize(ring_cap_));
    }
    SubscriberSlot* slotById(uint32_t sub_id);
};
