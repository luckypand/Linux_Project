#include "BufferPool.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <chrono>

// ============================================================
// 构造/析构
// ============================================================

BufferPool::BufferPool(const std::string& shm_name, uint32_t block_count, uint32_t max_payload)
    : shm_name_(shm_name)
    , is_creator_(true)
    , shm_fd_(-1)
    , total_size_(0)
    , mapped_ptr_(nullptr)
    , header_(nullptr)
    , free_next_(nullptr)
    , blocks_base_(nullptr)
{
    // 计算 block_stride：header + payload，向上对齐到 64B
    uint32_t stride = alignUp(sizeof(BlockHeader) + max_payload);

    total_size_ = PoolHeader::requiredSize(block_count, stride);

    // ---- 创建 SHM 段 ----
    shm_unlink(shm_name_.c_str());  // 清理可能残留的同名段

    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd_ < 0) {
        throw std::runtime_error("BufferPool: shm_open (CREATE) failed for '" + shm_name_ + "'");
    }

    if (ftruncate(shm_fd_, static_cast<off_t>(total_size_)) < 0) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        throw std::runtime_error("BufferPool: ftruncate failed");
    }

    mapped_ptr_ = mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    close(shm_fd_);  // mmap 后 fd 可关闭
    shm_fd_ = -1;

    if (mapped_ptr_ == MAP_FAILED) {
        shm_unlink(shm_name_.c_str());
        throw std::runtime_error("BufferPool: mmap failed");
    }

    // ---- placement new 初始化 PoolHeader ----
    header_ = new (mapped_ptr_) PoolHeader();
    header_->free_head.store(UINT32_MAX, std::memory_order_relaxed);
    header_->block_count = block_count;
    header_->block_stride = stride;
    header_->max_payload = max_payload;
    header_->create_timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // ---- 计算各区域指针 ----
    free_next_   = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mapped_ptr_) + sizeof(PoolHeader));
    blocks_base_ = reinterpret_cast<uint8_t*>(free_next_ + block_count);

    // ---- 初始化所有 Block 状态为 FREE ----
    for (uint32_t i = 0; i < block_count; i++) {
        ShmDataBlock* blk = blockAt(i);
        blk->header.state.store(ShmBlockState::FREE, std::memory_order_relaxed);
        blk->header.ref_count.store(0, std::memory_order_relaxed);
        blk->header.block_id = i;
    }

    // ---- 初始化 free 链表 ----
    initFreeList();
}

BufferPool::BufferPool(const std::string& shm_name)
    : shm_name_(shm_name)
    , is_creator_(false)
    , shm_fd_(-1)
    , total_size_(0)
    , mapped_ptr_(nullptr)
    , header_(nullptr)
    , free_next_(nullptr)
    , blocks_base_(nullptr)
{
    // ---- ATTACH：打开已有 SHM 段 ----
    shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
    if (shm_fd_ < 0) {
        throw std::runtime_error("BufferPool: shm_open (ATTACH) failed for '" + shm_name_ + "'");
    }

    // 先映射 PoolHeader 大小，读取元数据获取完整大小
    // 注意：这里假设 PoolHeader 在段首，且至少 sizeof(PoolHeader) 可读
    void* probe = mmap(nullptr, sizeof(PoolHeader), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (probe == MAP_FAILED) {
        close(shm_fd_);
        throw std::runtime_error("BufferPool: mmap probe failed");
    }

    auto* hdr = static_cast<PoolHeader*>(probe);
    uint32_t block_count = hdr->block_count;
    uint32_t stride      = hdr->block_stride;
    total_size_ = PoolHeader::requiredSize(block_count, stride);

    munmap(probe, sizeof(PoolHeader));

    // 用完整大小重新 mmap
    mapped_ptr_ = mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    close(shm_fd_);
    shm_fd_ = -1;

    if (mapped_ptr_ == MAP_FAILED) {
        throw std::runtime_error("BufferPool: mmap full segment failed");
    }

    // ---- 定位各区域 ----
    header_      = static_cast<PoolHeader*>(mapped_ptr_);
    free_next_   = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mapped_ptr_) + sizeof(PoolHeader));
    blocks_base_ = reinterpret_cast<uint8_t*>(free_next_ + header_->block_count);
}

BufferPool::~BufferPool()
{
    if (mapped_ptr_ && mapped_ptr_ != MAP_FAILED) {
        munmap(mapped_ptr_, total_size_);
    }
    // 只有创建者负责 shm_unlink（从命名空间中移除）
    if (is_creator_) {
        shm_unlink(shm_name_.c_str());
    }
}

// ============================================================
// 块操作
// ============================================================

uint32_t BufferPool::allocate()
{
    uint32_t head = header_->free_head.load(std::memory_order_acquire);

    while (head != UINT32_MAX) {
        uint32_t next = free_next_[head];

        // CAS 弹出 free 栈顶
        if (header_->free_head.compare_exchange_weak(head, next,
                std::memory_order_release, std::memory_order_relaxed))
        {
            // 成功取到 Block head，CAS 设置状态 FREE → WRITING
            ShmDataBlock* blk = blockAt(head);
            ShmBlockState expected = ShmBlockState::FREE;
            if (blk->header.state.compare_exchange_strong(expected, ShmBlockState::WRITING,
                    std::memory_order_acquire, std::memory_order_relaxed))
            {
                return head;
            }
            // 理论上不会到这里（FREE 栈取出的 block 必定是 FREE 状态）
            // 如果发生了，把 block 推回 free 栈
            uint32_t old_head = header_->free_head.load(std::memory_order_relaxed);
            do {
                free_next_[head] = old_head;
            } while (!header_->free_head.compare_exchange_weak(old_head, head,
                    std::memory_order_release, std::memory_order_relaxed));
            return UINT32_MAX;
        }
        // CAS 失败，head 已被更新，重试
    }

    return UINT32_MAX;  // 无空闲 Block
}

void BufferPool::commitReady(uint32_t block_id, uint32_t size, uint64_t timestamp, QoSType qos)
{
    ShmDataBlock* blk = blockAt(block_id);
    blk->header.payload_size = size;
    blk->header.timestamp    = timestamp;
    blk->header.qos          = qos;

    // release store: 确保 payload 写入先于状态变更对所有观察者可见
    blk->header.state.store(ShmBlockState::READY, std::memory_order_release);
}

ShmDataBlock* BufferPool::get(uint32_t block_id)
{
    if (block_id >= header_->block_count) return nullptr;
    return blockAt(block_id);
}

const ShmDataBlock* BufferPool::get(uint32_t block_id) const
{
    if (block_id >= header_->block_count) return nullptr;
    return blockAt(block_id);
}

void BufferPool::release(uint32_t block_id)
{
    if (block_id >= header_->block_count) return;

    ShmDataBlock* blk = blockAt(block_id);

    // 引用计数减一（acq_rel 确保读/写完成后再减）
    uint32_t old_ref = blk->header.ref_count.fetch_sub(1, std::memory_order_acq_rel);

    if (old_ref == 1) {
        // 我是最后一个引用者，负责回收
        // CAS READY → FREE
        ShmBlockState expected = ShmBlockState::READY;
        if (blk->header.state.compare_exchange_strong(expected, ShmBlockState::FREE,
                std::memory_order_release, std::memory_order_relaxed))
        {
            // 推回 free 栈
            uint32_t head = header_->free_head.load(std::memory_order_relaxed);
            do {
                free_next_[block_id] = head;
            } while (!header_->free_head.compare_exchange_weak(head, block_id,
                    std::memory_order_release, std::memory_order_relaxed));
        }
        // 如果 CAS 失败，说明 Block 已被其他人回收（不应发生），这是 bug 的信号
    }
    // old_ref == 0 表示多 release 了，调用方保证不重复
}

// ============================================================
// 查询
// ============================================================

uint32_t BufferPool::freeCount() const
{
    uint32_t count = 0;
    uint32_t head = header_->free_head.load(std::memory_order_acquire);
    while (head != UINT32_MAX) {
        count++;
        if (count > header_->block_count) break;  // 防御：free 链表不应长于 block 总数
        head = free_next_[head];
    }
    return count;
}

// ============================================================
// 内部辅助
// ============================================================

void BufferPool::initFreeList()
{
    uint32_t n = header_->block_count;
    for (uint32_t i = 0; i < n; i++) {
        free_next_[i] = (i + 1 < n) ? (i + 1) : UINT32_MAX;
    }
    header_->free_head.store(0, std::memory_order_release);
}
