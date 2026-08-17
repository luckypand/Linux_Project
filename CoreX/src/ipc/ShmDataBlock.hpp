#pragma once
#include <atomic>
#include <cstdint>
#include "QosPolicy.hpp"

// ============================================================
// 共享内存数据块定义 — Pub/Sub 系统的最小数据单元
// ============================================================

// Block 状态机（无锁协议核心）：
//   FREE ──CAS──▶ WRITING ──store(release)──▶ READY ──CAS(ref_count==0)──▶ FREE
//
// - 只有持有 WRITING 的生产者可以写 payload
// - READY 状态下多个消费者可以并发读取
// - ref_count 归零时，最后离开的消费者负责将 Block 回收到 FREE
enum class ShmBlockState : uint8_t
{
    FREE    = 0,  // 空闲，可被分配
    WRITING = 1,  // 生产者正在写入，消费者不可读
    READY   = 2   // 数据就绪，消费者可读
};

// Block 头部元数据（独立 cacheline，alignas(64) 防止伪共享）
// 所有跨进程/跨线程的并发控制字段均使用 std::atomic
struct alignas(64) BlockHeader
{
    // --- 并发控制字段（atomic）---
    std::atomic<ShmBlockState> state{ShmBlockState::FREE};  // 当前状态
    std::atomic<uint32_t>      ref_count{0};                // 引用计数：生产者和消费者共享同一 Block 的关键

    // --- 只读元数据（状态为 READY 后不可变）---
    uint32_t block_id;       // 块编号（在 BufferPool 中的索引，O(1) 定位）
    uint32_t topic_id;       // 所属 Topic 编号
    uint64_t timestamp;      // 发布时间戳（微秒，从 steady_clock 获取）
    uint32_t payload_size;   // 实际写入的数据大小（字节）
    QoSType  qos;            // 发布时的 QoS 类型
    uint8_t  _padding[3];    // 对齐填充，确保整体 64 字节
};

// 编译期验证 cacheline 对齐
static_assert(sizeof(BlockHeader) == 64, "BlockHeader must be exactly 64 bytes (one cacheline)");

// 完整共享内存数据块
// 实际内存布局：[BlockHeader 64B] [payload... max_payload_size 字节]
// BufferPool 在创建时按 stride = sizeof(BlockHeader) + max_payload_size 分配
struct ShmDataBlock
{
    BlockHeader header;

    // 获取 payload 起始地址
    uint8_t*       payload()       { return reinterpret_cast<uint8_t*>(this) + sizeof(BlockHeader); }
    const uint8_t* payload() const { return reinterpret_cast<const uint8_t*>(this) + sizeof(BlockHeader); }
};
