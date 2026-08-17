#include "SubscriberRegistry.hpp"
#include <cstring>

void SubscriberRegistry::init(void* shm_base, uint32_t max_slots, uint32_t ring_cap)
{
    base_     = static_cast<uint8_t*>(shm_base);
    header_   = static_cast<RegistryHeader*>(shm_base);
    ring_cap_ = ring_cap;

    header_->max_slots     = max_slots;
    header_->ring_capacity = ring_cap;
    header_->active_count.store(0, std::memory_order_relaxed);
    header_->next_sub_id.store(0, std::memory_order_relaxed);

    // 初始化所有槽位
    for (uint32_t i = 0; i < max_slots; i++) {
        SubscriberSlot* s = slotByIndex(i);
        s->state.store(SubState::INACTIVE, std::memory_order_relaxed);
        s->heartbeat.store(0, std::memory_order_relaxed);
        s->subscriber_id = 0;

        BlockIdRing* ring = s->ring();
        new (ring) BlockIdRing();
        ring->init(ring_cap);
    }
}

void SubscriberRegistry::attach(void* shm_base)
{
    base_     = static_cast<uint8_t*>(shm_base);
    header_   = static_cast<RegistryHeader*>(shm_base);
    ring_cap_ = header_->ring_capacity;
    // 不重置任何状态，只映射已有数据结构
}

int32_t SubscriberRegistry::addSubscriber()
{
    for (uint32_t i = 0; i < header_->max_slots; i++) {
        SubscriberSlot* s = slotByIndex(i);

        SubState expected = SubState::INACTIVE;
        if (s->state.compare_exchange_strong(expected, SubState::ACTIVE,
                std::memory_order_acquire, std::memory_order_relaxed))
        {
            uint32_t sub_id = header_->next_sub_id.fetch_add(1, std::memory_order_relaxed);
            s->subscriber_id = sub_id;
            s->heartbeat.store(0, std::memory_order_relaxed);

            BlockIdRing* ring = s->ring();
            ring->init(ring_cap_);  // 清空可能残留的数据

            header_->active_count.fetch_add(1, std::memory_order_relaxed);
            return static_cast<int32_t>(sub_id);
        }
    }
    return -1;
}

void SubscriberRegistry::removeSubscriber(uint32_t sub_id, std::vector<uint32_t>& out_blocks)
{
    SubscriberSlot* s = slotById(sub_id);
    if (!s) return;

    s->state.store(SubState::INACTIVE, std::memory_order_release);

    BlockIdRing* ring = s->ring();
    uint32_t bid;
    while (ring->pop(bid)) {
        out_blocks.push_back(bid);
    }

    header_->active_count.fetch_sub(1, std::memory_order_relaxed);
}

void SubscriberRegistry::pushToAll(uint32_t block_id, std::vector<uint32_t>& out_overwritten)
{
    for (uint32_t i = 0; i < header_->max_slots; i++) {
        SubscriberSlot* s = slotByIndex(i);
        if (s->state.load(std::memory_order_acquire) != SubState::ACTIVE)
            continue;

        uint32_t ov = s->ring()->push(block_id);
        if (ov != UINT32_MAX)
            out_overwritten.push_back(ov);
    }
}

uint32_t SubscriberRegistry::pushToOne(uint32_t sub_id, uint32_t block_id)
{
    SubscriberSlot* s = slotById(sub_id);
    if (!s) return UINT32_MAX;
    return s->ring()->push(block_id);
}

bool SubscriberRegistry::popFor(uint32_t sub_id, uint32_t& out_block_id)
{
    SubscriberSlot* s = slotById(sub_id);
    if (!s) return false;
    return s->ring()->pop(out_block_id);
}

void SubscriberRegistry::heartbeat(uint32_t sub_id, uint64_t now_us)
{
    SubscriberSlot* s = slotById(sub_id);
    if (!s) return;
    s->heartbeat.store(now_us, std::memory_order_relaxed);
}

std::vector<uint32_t> SubscriberRegistry::checkTimeout(uint64_t now_us, uint64_t timeout_us)
{
    std::vector<uint32_t> timed_out;
    for (uint32_t i = 0; i < header_->max_slots; i++) {
        SubscriberSlot* s = slotByIndex(i);
        if (s->state.load(std::memory_order_acquire) != SubState::ACTIVE)
            continue;

        uint64_t hb = s->heartbeat.load(std::memory_order_acquire);
        if (hb > 0 && (now_us - hb) > timeout_us) {
            SubState expected = SubState::ACTIVE;
            if (s->state.compare_exchange_strong(expected, SubState::TIMEOUT,
                    std::memory_order_release, std::memory_order_relaxed))
            {
                timed_out.push_back(s->subscriber_id);
            }
        }
    }
    return timed_out;
}

uint32_t SubscriberRegistry::activeCount() const
{
    return header_->active_count.load(std::memory_order_acquire);
}

SubscriberSlot* SubscriberRegistry::slotById(uint32_t sub_id)
{
    for (uint32_t i = 0; i < header_->max_slots; i++) {
        SubscriberSlot* s = slotByIndex(i);
        if (s->subscriber_id == sub_id &&
            s->state.load(std::memory_order_acquire) != SubState::INACTIVE)
        {
            return s;
        }
    }
    return nullptr;
}
