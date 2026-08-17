#include "TopicChannel.hpp"
#include "BufferPool.hpp"
#include <cstring>

// ============================================================
// 初始化
// ============================================================

void TopicChannel::init(void* shm_base, uint32_t topic_id, const QosPolicy& qos,
                        BufferPool* pool, uint32_t max_subs, uint32_t ring_cap)
{
    pool_  = pool;

    // placement new 头部
    header_ = new (shm_base) TopicChannelHeader();
    header_->topic_id       = topic_id;
    header_->qos_type       = qos.type;
    header_->qos_depth      = qos.depth;
    header_->max_subscribers = max_subs;
    header_->ring_capacity  = ring_cap;

    // SubscriberRegistry 紧跟头部之后
    uint8_t* reg_base = static_cast<uint8_t*>(shm_base) + sizeof(TopicChannelHeader);
    registry_.init(reg_base, max_subs, ring_cap);

    // HistoryCache 进程本地
    history_.init(qos.type, qos.depth);
}

void TopicChannel::attach(void* shm_base, BufferPool* pool)
{
    pool_   = pool;
    header_ = static_cast<TopicChannelHeader*>(shm_base);

    uint8_t* reg_base = static_cast<uint8_t*>(shm_base) + sizeof(TopicChannelHeader);
    registry_.attach(reg_base);  // 只映射，不重置

    // 消费者不需要 history
}

// ============================================================
// 生产者：发布数据
// ============================================================

uint32_t TopicChannel::publish(const void* data, uint32_t size)
{
    if (!pool_ || size > pool_->maxPayload())
        return UINT32_MAX;

    // 1. 分配 Block
    uint32_t block_id = pool_->allocate();
    if (block_id == UINT32_MAX)
        return UINT32_MAX;

    ShmDataBlock* blk = pool_->get(block_id);

    // 2. 写入 payload
    memcpy(blk->payload(), data, size);

    // 3. 设置元数据 + 状态转换 WRITING → READY
    blk->header.topic_id = header_->topic_id;
    auto now_ts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    pool_->commitReady(block_id, size, now_ts, header_->qos_type);

    // 4. 计算引用计数（必须先加后入队）
    uint32_t active = registry_.activeCount();
    uint32_t hist_ref = (header_->qos_type != QoSType::STREAM) ? 1 : 0;
    uint32_t total_refs = active + hist_ref;

    if (total_refs == 0) {
        // 无订阅者且无历史：直接回收 Block 回 FREE（不走 release，因为 ref_count==0）
        blk->header.state.store(ShmBlockState::FREE, std::memory_order_release);
        // 推回 free 栈
        // 直接操作 pool 的内部 free 栈比较复杂，这里用 release 语义补丁：
        // 设 ref_count=1 然后 release，驱动回收路径
        blk->header.ref_count.store(1, std::memory_order_release);
        pool_->release(block_id);
        return block_id;
    }

    blk->header.ref_count.store(total_refs, std::memory_order_release);

    // 5. 推送到所有订阅者 Ring
    std::vector<uint32_t> overwritten;
    if (active > 0) {
        registry_.pushToAll(block_id, overwritten);
        // 释放被覆盖的旧 Block
        for (uint32_t ov : overwritten) {
            pool_->release(ov);
        }
    }

    // 6. 更新历史缓存（KEEP_LAST / LATCH）
    if (hist_ref) {
        uint32_t evicted = history_.push(block_id);
        if (evicted != UINT32_MAX) {
            pool_->release(evicted);
        }
    }

    return block_id;
}

// ============================================================
// 消费者：订阅 / 接收
// ============================================================

int32_t TopicChannel::subscribe()
{
    int32_t sub_id = registry_.addSubscriber();
    if (sub_id < 0) return -1;

    // 重放历史（KEEP_LAST / LATCH）— 只推送给新订阅者
    auto history = history_.getHistory();
    for (uint32_t bid : history) {
        ShmDataBlock* blk = pool_->get(bid);
        if (!blk) continue;

        // 为这个历史 Block 增加引用计数（新订阅者也引用它）
        blk->header.ref_count.fetch_add(1, std::memory_order_relaxed);

        // 只推送给新订阅者
        uint32_t ov = registry_.pushToOne(sub_id, bid);
        if (ov != UINT32_MAX) {
            pool_->release(ov);
        }
    }

    return sub_id;
}

void TopicChannel::unsubscribe(uint32_t sub_id)
{
    std::vector<uint32_t> blocks;
    registry_.removeSubscriber(sub_id, blocks);

    // 释放该订阅者积压的所有 Block
    for (uint32_t bid : blocks) {
        pool_->release(bid);
    }
}

bool TopicChannel::receive(uint32_t sub_id, void* out_buf, uint32_t& in_out_size)
{
    uint32_t block_id;
    if (!registry_.popFor(sub_id, block_id))
        return false;

    ShmDataBlock* blk = pool_->get(block_id);
    if (!blk)
        return false;

    // 确认数据就绪
    if (blk->header.state.load(std::memory_order_acquire) != ShmBlockState::READY) {
        // 不应该发生，但防御性处理
        pool_->release(block_id);
        return false;
    }

    // 拷贝数据
    uint32_t copy_size = (in_out_size < blk->header.payload_size)
                         ? in_out_size : blk->header.payload_size;
    memcpy(out_buf, blk->payload(), copy_size);
    in_out_size = blk->header.payload_size;  // 返回实际大小

    // 释放引用
    pool_->release(block_id);

    return true;
}

void TopicChannel::heartbeat(uint32_t sub_id, uint64_t now_us)
{
    registry_.heartbeat(sub_id, now_us);
}

std::vector<uint32_t> TopicChannel::checkTimeout(uint64_t now_us, uint64_t timeout_us)
{
    return registry_.checkTimeout(now_us, timeout_us);
}
