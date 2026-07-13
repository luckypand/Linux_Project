#pragma once
// ============================================================================
// ShmImageTransporter.hpp — 高频图像/点云共享内存快速通道
//
// 原理：三缓冲 (Triple Buffer) + 原子索引，ROS 回调写入，RPC 线程读取
//
//   ROS Camera Driver (30fps, ~6MB/frame)
//       ↓ ros::Subscriber 回调
//   ┌──────────────────────────────────────────────────────┐
//   │  ShmImageTransporter                                 │
//   │                                                      │
//   │  ShmMeta (共享内存头部, cacheline 对齐)                │
//   │  ┌────────────────────────────────────────────────┐  │
//   │  │ atomic<uint32_t> write_idx    ← ROS 线程更新    │  │
//   │  │ atomic<uint32_t> read_idx     ← RPC 线程更新    │  │
//   │  │ uint32_t frame_size[3]                         │  │
//   │  │ uint64_t timestamp[3]                          │  │
//   │  │ char     encoding[3][32]                       │  │
//   │  │ uint32_t width[3], height[3], step[3]          │  │
//   │  └────────────────────────────────────────────────┘  │
//   │                                                      │
//   │  Frame Buffer[0]  ← 可写入                            │
//   │  Frame Buffer[1]  ← 可读取                            │
//   │  Frame Buffer[2]  ← 备用（消除读写竞争）                │
//   └──────────────────────────────────────────────────────┘
//       ↓ (无锁三缓冲，延迟 < 1ms)
//   CoreX RPC Response
//
// 使用方式：
//   // 创建（指定最大帧大小和共享内存名称）
//   ShmImageTransporter transporter("/camera_image_shm", 10*1024*1024);
//
//   // ROS 回调中写入：
//   transporter.write(imageData, size, width, height, encoding, step);
//
//   // RPC Handler 中读取：
//   if (transporter.read(outData, outMeta)) { ... }
//
// 继承已有基础设施：ShmMemoryPool (src/ipc/ShmSegment.hpp)
// ============================================================================

#include <string>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cstddef>

// 前向声明
class ShmMemoryPool;

// ============================================================================
// 共享内存元数据 (cacheline 对齐, 防止 false sharing)
// ============================================================================
struct alignas(64) ShmImageMeta
{
    static constexpr int NUM_BUFFERS = 3;     // 三缓冲

    // 读写索引（由各自的线程更新）
    std::atomic<uint32_t> write_idx{0};        // ROS 线程：下一帧写入位置
    std::atomic<uint32_t> read_idx{0};         // RPC 线程：最新可读帧位置
    std::atomic<uint32_t> frame_ready{0};      // bitmask：哪几帧已就绪

    // 每帧元数据
    uint32_t frame_size[NUM_BUFFERS];          // 帧数据大小 (bytes)
    uint64_t timestamp[NUM_BUFFERS];           // 时间戳 (us)
    uint32_t width[NUM_BUFFERS];
    uint32_t height[NUM_BUFFERS];
    uint32_t step[NUM_BUFFERS];
    char     encoding[NUM_BUFFERS][32];        // "rgb8", "bgr8" 等

    char     _pad[28];  // 填充至 cacheline 边界
};
static_assert(sizeof(ShmImageMeta) % 64 == 0,
              "ShmImageMeta must be cacheline-aligned");

// ============================================================================
// 读取结果
// ============================================================================
struct ImageFrame
{
    const uint8_t* data = nullptr;
    uint32_t       size = 0;
    uint32_t       width = 0;
    uint32_t       height = 0;
    uint32_t       step = 0;
    uint64_t       timestamp = 0;
    char           encoding[32] = {};
    uint32_t       frame_index = 0;  // 当前读取的帧编号
};

// ============================================================================
// ShmImageTransporter
// ============================================================================
class ShmImageTransporter
{
public:
    // @param shmName        共享内存段名称
    // @param maxFrameSize   单帧最大大小 (bytes)，如 10*1024*1024 = 10MB
    // @param isCreator      是否创建者（通常 ROS 写入方为 true）
    ShmImageTransporter(const std::string& shmName,
                        size_t maxFrameSize,
                        bool isCreator = true);
    ~ShmImageTransporter();

    // 禁止拷贝
    ShmImageTransporter(const ShmImageTransporter&) = delete;
    ShmImageTransporter& operator=(const ShmImageTransporter&) = delete;

    // ---- 写入接口 (ROS 线程调用) ----
    // @param data      图像数据
    // @param size      数据大小
    // @param width     图像宽度
    // @param height    图像高度
    // @param encoding  编码格式 ("rgb8", "bgr8" ...)
    // @param step      每行字节数
    // @param timestamp 时间戳 (us)，0 表示自动生成
    // @return true 写入成功
    bool write(const uint8_t* data, uint32_t size,
               uint32_t width, uint32_t height,
               const char* encoding, uint32_t step,
               uint64_t timestamp = 0);

    // ---- 读取接口 (RPC 线程调用) ----
    // @param frame 输出：最新帧数据引用（指向共享内存，不拷贝）
    // @return true 有可用帧
    bool read(ImageFrame& frame);

    // 等待新帧（用于阻塞读取模式）
    // @param timeoutMs 超时 (ms)，-1 表示永久等待
    bool readWait(ImageFrame& frame, int timeoutMs = 100);

    // ---- 查询接口 ----
    const std::string& shmName() const { return shmName_; }
    size_t maxFrameSize() const { return maxFrameSize_; }
    bool isActive() const { return pool_ != nullptr; }

private:
    // 获取指定帧的数据指针
    uint8_t* frameDataPtr(int index);
    const uint8_t* frameDataPtr(int index) const;

    std::string shmName_;
    size_t      maxFrameSize_;
    bool        isCreator_;

    std::unique_ptr<ShmMemoryPool> pool_;  // 复用已有 SHM 基础设施
    ShmImageMeta* meta_ = nullptr;          // → 共享内存中的元数据
    uint8_t*     frameBase_ = nullptr;      // → 帧数据起始地址
};
