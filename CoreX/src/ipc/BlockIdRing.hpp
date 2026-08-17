#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <climits>

// ============================================================
// BlockIdRing — 无锁环形缓冲，只存 uint32_t Block ID
// ============================================================
//
// 设计要点：
//   - SPSC（单生产者单消费者）无锁安全，不需要 CAS 抢占槽位
//   - 容量必须为 2 的幂，取模用位运算 & (capacity_mask)
//   - 满时覆盖最旧的 Block ID，被覆盖的 id 返回给调用方去 release
//   - 所有字段对齐到独立 cacheline，防止伪共享
//   - 可 placement new 到共享内存中（无虚函数、无堆分配）
//
// 使用方式（共享内存场景）：
//   size_t total = BlockIdRing::requiredSize(256);
//   void* shm = ...; // mmap 的共享内存
//   BlockIdRing* ring = new (shm) BlockIdRing();
//   ring->init(256);

struct alignas(64) BlockIdRing
{
    // ---- 独立 cacheline：生产者独占 ----
    alignas(64) std::atomic<uint32_t> write_index{0};

    // ---- 独立 cacheline：消费者独占 ----
    alignas(64) std::atomic<uint32_t> read_index{0};

    // ---- 只读配置（初始化后不变）----
    uint32_t capacity;       // 总容量（2 的幂）
    uint32_t capacity_mask;  // capacity - 1，位运算取模用

    // ============================================================
    // 生命周期（placement new 兼容共享内存）
    // ============================================================

    BlockIdRing() = default;
    ~BlockIdRing() = default;

    // 初始化：在 placement new 后调用，不能在构造函数里做因为参数未知
    void init(uint32_t cap)
    {
        capacity      = cap;
        capacity_mask = cap - 1;
        write_index.store(0, std::memory_order_relaxed);
        read_index.store(0, std::memory_order_relaxed);
    }

    // ============================================================
    // 数据区访问（buffer 紧跟结构体之后）
    // ============================================================

    uint32_t*       buffer()       { return reinterpret_cast<uint32_t*>(this + 1); }
    const uint32_t* buffer() const { return reinterpret_cast<const uint32_t*>(this + 1); }

    // 计算整个结构体所需的共享内存字节数
    static constexpr size_t requiredSize(uint32_t capacity)
    {
        return sizeof(BlockIdRing) + capacity * sizeof(uint32_t);
    }

    // ============================================================
    // 生产者 API
    // ============================================================

    // 写入一个 Block ID
    // 返回值：被覆盖的 Block ID（需要调用方去 release），UINT32_MAX 表示没有覆盖
    // 满时覆盖最旧的策略：保证新数据永远不丢
    uint32_t push(uint32_t block_id)
    {
        uint32_t w = write_index.load(std::memory_order_relaxed);
        uint32_t r = read_index.load(std::memory_order_acquire);

        uint32_t next_w = (w + 1) & capacity_mask;
        uint32_t overwritten = UINT32_MAX;

        if (next_w == r)
        {
            // 满了：覆盖最旧的一项
            overwritten = buffer()[r];
            r = (r + 1) & capacity_mask;
            read_index.store(r, std::memory_order_release);
        }

        buffer()[w] = block_id;
        write_index.store(next_w, std::memory_order_release);
        return overwritten;
    }

    // ============================================================
    // 消费者 API
    // ============================================================

    // 读取一个 Block ID，返回 true 表示成功
    bool pop(uint32_t& out_block_id)
    {
        uint32_t r = read_index.load(std::memory_order_relaxed);
        uint32_t w = write_index.load(std::memory_order_acquire);

        if (r == w) return false;  // 队列为空

        out_block_id = buffer()[r];
        read_index.store((r + 1) & capacity_mask, std::memory_order_release);
        return true;
    }

    // 非破坏性查看队首
    bool peek(uint32_t& out_block_id) const
    {
        uint32_t r = read_index.load(std::memory_order_acquire);
        uint32_t w = write_index.load(std::memory_order_acquire);

        if (r == w) return false;
        out_block_id = buffer()[r];
        return true;
    }

    // 跳过队首（不读取数据，只推进 read_index）
    bool skip()
    {
        uint32_t r = read_index.load(std::memory_order_relaxed);
        uint32_t w = write_index.load(std::memory_order_acquire);

        if (r == w) return false;
        read_index.store((r + 1) & capacity_mask, std::memory_order_release);
        return true;
    }

    // ============================================================
    // 状态查询
    // ============================================================

    bool empty() const
    {
        return read_index.load(std::memory_order_acquire) ==
               write_index.load(std::memory_order_acquire);
    }

    uint32_t size() const
    {
        uint32_t w = write_index.load(std::memory_order_acquire);
        uint32_t r = read_index.load(std::memory_order_acquire);
        if (w >= r) return w - r;
        return capacity - r + w;
    }

    bool full() const
    {
        uint32_t next_w = (write_index.load(std::memory_order_acquire) + 1) & capacity_mask;
        return next_w == read_index.load(std::memory_order_acquire);
    }
};

// 编译期验证：两个索引字段各占独立 cacheline
static_assert(sizeof(BlockIdRing) == 128, "BlockIdRing header: 2 cachelines (write_index + read_index + 2*uint32_t)");
