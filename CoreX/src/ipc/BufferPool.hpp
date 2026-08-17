#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include "ShmDataBlock.hpp"

// ============================================================
// BufferPool — 共享内存块池
// ============================================================
//
// 职责三件事：
//   1. 管理共享内存中的 Block 数组（创建/附加）
//   2. 分配数据块（从 free 栈 CAS 弹出）
//   3. 回收数据块（引用计数归零后 CAS 推回 free 栈）
//
// 共享内存布局（一个 SHM 段）：
// ┌──────────────────────────┐  ← mmap 起始地址
// │  PoolHeader (64B,对齐)    │  free_head + block_count + block_stride + max_payload
// ├──────────────────────────┤
// │  free_next[block_count]  │  隐式空闲链表，free_next[i]=下一个空闲 Block 的 index
// ├──────────────────────────┤
// │  blocks[0]               │  stride 字节（header 64B + payload max_payload, 对齐到 64B）
// │  blocks[1]               │
// │  ...                     │
// │  blocks[block_count-1]   │
// └──────────────────────────┘
//
// 分配/回收协议（无锁）：
//   allocate(): CAS 弹出 free 栈 → CAS FREE→WRITING → 返回 block_id
//   commitReady(): 设 size/ts/qos, WRITING→READY (release store)
//   release(): ref_count.fetch_sub(1, acq_rel), 看到 0 的线程负责 CAS READY→FREE + 推回 free 栈

// Pool 头部（放在共享内存最前面）
struct alignas(64) PoolHeader
{
    std::atomic<uint32_t> free_head{0};  // 空闲链表头（Block index），UINT32_MAX 表示无空闲 Block
    uint32_t block_count;                 // Block 总数
    uint32_t block_stride;               // 每个 Block 占用的字节数（已对齐到 64B）
    uint32_t max_payload;                // 每个 Block 的 payload 容量上限
    uint64_t create_timestamp;           // Pool 创建时间戳（微秒）

    // 计算整个 Pool 需要的共享内存总大小
    static size_t requiredSize(uint32_t block_count, uint32_t block_stride)
    {
        return sizeof(PoolHeader)
             + block_count * sizeof(uint32_t)     // free_next 数组
             + block_count * block_stride;         // Block 数组
    }
};

class BufferPool
{
public:
    // === 创建者：新建 SHM 段并初始化 Pool ===
    // shm_name: 共享内存名称（如 "/corex_pubsub_pool"）
    // block_count: Block 总数（建议 64~256）
    // max_payload: 每个 Block 最大 payload 大小（如 8MB）
    BufferPool(const std::string& shm_name, uint32_t block_count, uint32_t max_payload);

    // === 附加者：打开已有 SHM 段（只读 Pool 元数据，不 unlink）===
    explicit BufferPool(const std::string& shm_name);

    ~BufferPool();

    // 禁止拷贝
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // ============================================================
    // 块操作
    // ============================================================

    // 分配一个空闲 Block，CAS FREE→WRITING，返回 block_id
    // 返回值 UINT32_MAX 表示无空闲 Block
    uint32_t allocate();

    // 通过 block_id 获取 Block 指针（O(1) 直接计算偏移）
    ShmDataBlock*       get(uint32_t block_id);
    const ShmDataBlock* get(uint32_t block_id) const;

    // 引用计数减一（fetch_sub(1, acq_rel)）
    // 看到 0 的线程负责：CAS READY→FREE，推回 free 栈
    // 幂等安全：重复 release 同一 block 只会导致 ref_count 下溢（UB），调用方保证不重复
    void release(uint32_t block_id);

    // 生产者完成写入：设置 size/timestamp/qos，CAS WRITING→READY (release)
    void commitReady(uint32_t block_id, uint32_t size, uint64_t timestamp, QoSType qos);

    // ============================================================
    // 查询
    // ============================================================

    uint32_t blockCount()  const { return header_->block_count; }
    uint32_t maxPayload()  const { return header_->max_payload; }
    uint32_t blockStride() const { return header_->block_stride; }

    // 统计空闲 Block 数量（遍历 free 栈，O(free_count)，调试用）
    uint32_t freeCount() const;

    // 已分配（非空闲）Block 数量
    uint32_t liveCount() const { return header_->block_count - freeCount(); }

    bool isValid() const { return header_ != nullptr; }

private:
    std::string shm_name_;
    bool        is_creator_;      // 创建者负责 shm_unlink
    int         shm_fd_;          // shm_open 返回的 fd（mmap 后可关闭，但保留用于 unlink 判断）
    size_t      total_size_;      // mmap 的总大小
    void*       mapped_ptr_;      // mmap 起始地址

    PoolHeader*   header_;        // → 共享内存 PoolHeader
    uint32_t*     free_next_;     // → free_next 数组
    uint8_t*      blocks_base_;   // → Block 数组起始

    // 通过 block_id 计算 Block 地址（O(1) 偏移计算）
    ShmDataBlock* blockAt(uint32_t block_id)
    {
        return reinterpret_cast<ShmDataBlock*>(blocks_base_ + block_id * header_->block_stride);
    }
    const ShmDataBlock* blockAt(uint32_t block_id) const
    {
        return reinterpret_cast<const ShmDataBlock*>(blocks_base_ + block_id * header_->block_stride);
    }

    // 初始化 free 链表（0 → 1 → 2 → ... → N-1 → UINT32_MAX）
    void initFreeList();

    // 对齐到 64 字节边界
    static constexpr uint32_t alignUp(uint32_t v) { return (v + 63) & ~63u; }
};
