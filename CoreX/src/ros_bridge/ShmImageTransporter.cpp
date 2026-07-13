// ============================================================================
// ShmImageTransporter.cpp — 高频图像共享内存通道 实现
// ============================================================================

#include "ShmImageTransporter.hpp"
#include "../ipc/ShmSegment.hpp"
#include <cstring>
#include <chrono>
#include <thread>

// 共享内存总大小 = 元数据 + 三帧缓冲
static size_t calcTotalSize(size_t maxFrameSize)
{
    return sizeof(ShmImageMeta) + ShmImageMeta::NUM_BUFFERS * maxFrameSize;
}

// 获取当前时间戳 (us)
static uint64_t nowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ============================================================================
// 构造 / 析构
// ============================================================================
ShmImageTransporter::ShmImageTransporter(const std::string& shmName,
                                         size_t maxFrameSize, bool isCreator)
    : shmName_(shmName)
    , maxFrameSize_(maxFrameSize)
    , isCreator_(isCreator)
{
    size_t totalSize = calcTotalSize(maxFrameSize);

    try {
        pool_ = std::make_unique<ShmMemoryPool>(shmName, totalSize, isCreator);
    } catch (...) {
        // SHM 创建失败 — 回退模式
        pool_ = nullptr;
        meta_ = nullptr;
        frameBase_ = nullptr;
        return;
    }

    uint8_t* base = static_cast<uint8_t*>(pool_->data());
    meta_      = reinterpret_cast<ShmImageMeta*>(base);
    frameBase_ = base + sizeof(ShmImageMeta);

    // 如果是创建者，初始化元数据
    if (isCreator_) {
        memset(meta_, 0, sizeof(ShmImageMeta));
    }
}

ShmImageTransporter::~ShmImageTransporter()
{
    // ShmMemoryPool 析构时自动 unmap + unlink (如果是创建者)
}

// ============================================================================
// 帧数据指针
// ============================================================================
uint8_t* ShmImageTransporter::frameDataPtr(int index)
{
    return frameBase_ + index * maxFrameSize_;
}

const uint8_t* ShmImageTransporter::frameDataPtr(int index) const
{
    return frameBase_ + index * maxFrameSize_;
}

// ============================================================================
// 写入 (ROS 线程)
// ============================================================================
bool ShmImageTransporter::write(const uint8_t* data, uint32_t size,
                                uint32_t width, uint32_t height,
                                const char* encoding, uint32_t step,
                                uint64_t timestamp)
{
    if (!meta_ || !frameBase_) return false;
    if (size > maxFrameSize_) return false;  // 帧太大

    // 选择下一个写入帧：避免覆盖正在读取的帧
    uint32_t rIdx = meta_->read_idx.load(std::memory_order_acquire);
    uint32_t wIdx = meta_->write_idx.load(std::memory_order_relaxed);

    // 选择不在读取中的空闲帧
    uint32_t nextIdx = wIdx;
    for (int i = 0; i < ShmImageMeta::NUM_BUFFERS; i++) {
        nextIdx = (wIdx + 1 + i) % ShmImageMeta::NUM_BUFFERS;
        if (nextIdx != rIdx) break;  // 避开正在读取的帧
    }

    // 写入帧数据
    uint8_t* dst = frameDataPtr(nextIdx);
    memcpy(dst, data, size);

    // 更新元数据
    meta_->frame_size[nextIdx] = size;
    meta_->width[nextIdx]      = width;
    meta_->height[nextIdx]     = height;
    meta_->step[nextIdx]       = step;
    meta_->timestamp[nextIdx]  = (timestamp > 0) ? timestamp : nowUs();
    strncpy(meta_->encoding[nextIdx], encoding, sizeof(meta_->encoding[nextIdx]) - 1);
    meta_->encoding[nextIdx][sizeof(meta_->encoding[nextIdx]) - 1] = '\0';

    // 标记帧就绪并更新写索引
    uint32_t readyMask = meta_->frame_ready.load(std::memory_order_relaxed);
    readyMask |= (1u << nextIdx);
    meta_->frame_ready.store(readyMask, std::memory_order_release);
    meta_->write_idx.store(nextIdx, std::memory_order_release);

    return true;
}

// ============================================================================
// 读取 (RPC 线程, 非阻塞)
// ============================================================================
bool ShmImageTransporter::read(ImageFrame& frame)
{
    if (!meta_ || !frameBase_) return false;

    uint32_t wIdx = meta_->write_idx.load(std::memory_order_acquire);
    uint32_t rIdx = meta_->read_idx.load(std::memory_order_relaxed);

    // 检查写索引是否有新帧
    if (wIdx == rIdx) {
        // 可能还没更新（需要检查 frame_ready）
        uint32_t readyMask = meta_->frame_ready.load(std::memory_order_acquire);
        if (!(readyMask & (1u << wIdx))) {
            return false;  // 没有新帧
        }
    }

    // 读取最新帧
    rIdx = wIdx;
    uint32_t readyMask = meta_->frame_ready.load(std::memory_order_acquire);
    if (!(readyMask & (1u << rIdx))) {
        return false;  // 帧尚未完全写入
    }

    meta_->read_idx.store(rIdx, std::memory_order_release);

    // 填充结果
    frame.data   = frameDataPtr(rIdx);
    frame.size   = meta_->frame_size[rIdx];
    frame.width  = meta_->width[rIdx];
    frame.height = meta_->height[rIdx];
    frame.step   = meta_->step[rIdx];
    frame.timestamp = meta_->timestamp[rIdx];
    strncpy(frame.encoding, meta_->encoding[rIdx], sizeof(frame.encoding) - 1);
    frame.frame_index = rIdx;

    return true;
}

// ============================================================================
// 读取 (阻塞等待新帧)
// ============================================================================
bool ShmImageTransporter::readWait(ImageFrame& frame, int timeoutMs)
{
    if (!meta_) return false;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);

    while (true) {
        if (read(frame)) return true;

        if (timeoutMs >= 0 &&
            std::chrono::steady_clock::now() >= deadline) {
            return false;  // 超时
        }

        // 1ms 自旋等待（高频图像场景下等待时间极短）
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
