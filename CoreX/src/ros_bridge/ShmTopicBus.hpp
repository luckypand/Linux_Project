#pragma once
// ============================================================================
// ShmTopicBus.hpp — 基于共享内存的同机 Topic 发布/订阅总线
//
// 设计目标：
//   在同一台机器上用共享内存替代 ROS TCPROS，降低同机 Topic 通信延迟 5-10x。
//
// 核心数据结构 (每个 topic 一个 SHM 段):
//   ┌────────────────────────────────────────────┐
//   │  ShmTopicMeta (cacheline 对齐)              │
//   │  - magic, topic_name, max_msg_size         │
//   │  - subscriber_count, subscriber_fds[]      │
//   │  - write_seq (原子递增序列号)               │
//   ├────────────────────────────────────────────┤
//   │  ShmTopicSlot[0..N-1] (每个 cacheline 对齐) │
//   │  - seq_num (0=空闲, N=已发布)              │
//   │  - msg_size                                │
//   │  - data[max_msg_size]                      │
//   └────────────────────────────────────────────┘
//
// 协议:
//   发布: write_seq++ → 写入槽位[seq % slot_count] → store seq_num (release)
//         → 遍历 subscriber_fds, write(eventfd, 1) 通知
//   订阅: 等待 eventfd → 遍历槽位找最新 seq_num → 读取数据
//         → 本地 last_read_seq 独立追踪（不写入 SHM）
//
// 使用方式:
//   // 创建者端（发布者或 Broker）
//   ShmTopicBus bus("/camera/image_raw", 1024*1024, 16, true);
//   bus.publish(data, size);
//
//   // 订阅者端
//   ShmTopicBus bus("/camera/image_raw", 1024*1024, 16, false);
//   int efd = bus.subscribe();        // 获取 eventfd
//   // 将 efd 注册到 epoll/EventLoop
//   std::string msg;
//   if (bus.tryRecv(msg)) { ... }     // 非阻塞读取
//
// 继承已有基础设施:
//   - ShmMemoryPool (src/ipc/ShmSegment.hpp)  — shm_open + mmap
//   - 无锁环形缓冲区设计理念 (src/ipc/RingBuffer.hpp)
// ============================================================================

#include <string>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <mutex>

class ShmMemoryPool;

// ============================================================================
// 常量
// ============================================================================
constexpr size_t   SHM_TOPIC_MAX_SLOTS       = 16;     // 环形槽位数
constexpr size_t   SHM_TOPIC_MAX_SUBSCRIBERS  = 8;      // 最大订阅者数
constexpr uint32_t SHM_TOPIC_MAGIC           = 0x43544F50; // "CTOP" (CoreX TOPic)
constexpr size_t   SHM_TOPIC_SLOT_ALIGN      = 64;     // cacheline 对齐

// ============================================================================
// 共享内存布局结构体 (POD, 可直接 mmap)
// ============================================================================

struct alignas(64) ShmTopicMeta
{
    // ---- 基础信息 ----
    uint32_t magic;                                    // 魔数校验 0x43544F50
    uint32_t max_msg_size;                             // 单消息最大字节数
    char     topic_name[64];                           // ROS Topic 名称
    char     ros_type[64];                             // ROS 消息类型（如 "geometry_msgs/Twist"）
    int32_t  slot_count;                               // 实际槽位数 (≤ SHM_TOPIC_MAX_SLOTS)
    int32_t  subscriber_count;                         // 当前订阅者数
    int32_t  subscriber_fds[SHM_TOPIC_MAX_SUBSCRIBERS]; // 订阅者 eventfd 列表（-1 = 空闲）

    // ---- 发布者状态 (独立 cacheline, 防止 false sharing) ----
    alignas(64) std::atomic<uint64_t> write_seq{0};    // 单调递增的全局写入序号
};

struct alignas(64) ShmTopicSlot
{
    std::atomic<uint64_t> seq_num{0};  // 0 = 空闲, N = 此槽位的消息序列号
    uint32_t msg_size;                 // 实际消息大小 (bytes)
    uint32_t _pad;
    // 注意: 消息数据紧跟在结构体之后 (alignas(64) 使 sizeof = 64)
    // data_ptr = reinterpret_cast<const uint8_t*>(&slot) + sizeof(ShmTopicSlot)
};

// ============================================================================
// 辅助函数: 计算 SHM 总大小
// ============================================================================
inline size_t ShmTopicTotalSize(size_t maxMsgSize, int slotCount)
{
    return sizeof(ShmTopicMeta) +
           slotCount * (sizeof(ShmTopicSlot) + maxMsgSize);
}

// ============================================================================
// ShmTopicBus
// ============================================================================
class ShmTopicBus
{
public:
    // ---- 构造 / 析构 ----
    //
    // @param topicName   ROS Topic 名称, 如 "/camera/image_raw"
    // @param maxMsgSize  单消息最大字节数
    // @param slotCount   环形槽位数 (默认 16, 最大 SHM_TOPIC_MAX_SLOTS)
    // @param isCreator   true=创建 SHM 段, false=附加已有段
    ShmTopicBus(const std::string& topicName, size_t maxMsgSize,
                int slotCount = 16, bool isCreator = false);
    ~ShmTopicBus();

    // 禁止拷贝
    ShmTopicBus(const ShmTopicBus&) = delete;
    ShmTopicBus& operator=(const ShmTopicBus&) = delete;

    // ---- 发布者 API ----

    // 向 SHM 发布一条消息
    // @param data  消息数据指针
    // @param size  消息大小 (bytes, 必须 ≤ maxMsgSize)
    // @return true 发布成功, false 消息过大或 SHM 无效
    bool publish(const void* data, size_t size);

    // ---- 订阅者 API ----

    // 订阅此 topic
    // @return eventfd (>= 0)，可注册到 epoll/EventLoop；< 0 表示失败
    // 调用者负责在不再需要时 close(fd)
    int subscribe();

    // 取消订阅
    // @param fd  之前 subscribe() 返回的 eventfd
    void unsubscribe(int fd);

    // 非阻塞读取最新消息
    // @param outMsg  输出: 消息数据 (原始 bytes)
    // @return true 有可读消息, false 无新消息
    bool tryRecv(std::string& outMsg);

    // 阻塞等待并读取最新消息
    // @param outMsg     输出: 消息数据
    // @param timeoutMs  超时 (ms), -1 = 永久等待
    // @return true 读到消息, false 超时
    bool recvWait(std::string& outMsg, int timeoutMs = 1000);

    // ---- 查询 ----

    const std::string& topicName() const { return topicName_; }
    bool isValid() const { return meta_ != nullptr; }
    int subscriberCount() const;
    uint64_t lastWriteSeq() const;
    size_t maxMsgSize() const { return maxMsgSize_; }

    // ---- 静态工具 ----

    // 检查指定名称的 SHM topic 段是否存在
    static bool exists(const std::string& topicName);

    // 生成 SHM 名称 (从 ROS topic 名)
    static std::string shmNameForTopic(const std::string& rosTopic);

private:
    // 获取槽位 i 的数据指针
    uint8_t*       slotData(int index);
    const uint8_t* slotData(int index) const;

    // 获取槽位 i 的结构体指针
    ShmTopicSlot*       slotAt(int index);
    const ShmTopicSlot* slotAt(int index) const;

    // 排空 eventfd 计数器
    static void drainEventfd(int fd);

    std::string topicName_;
    size_t      maxMsgSize_;
    int         slotCount_;
    bool        isCreator_;

    std::unique_ptr<ShmMemoryPool> pool_;   // SHM RAII
    ShmTopicMeta* meta_ = nullptr;           // → SHM 头部
    uint8_t*     slotsBase_ = nullptr;       // → 第一个槽位的起始地址

    // 订阅者本地状态 (不写入 SHM)
    uint64_t    lastReadSeq_ = 0;            // 本订阅者最后读取的序列号
    int         localEventFd_ = -1;          // 本订阅者的 eventfd

    // 线程安全保护 (订阅/取消订阅操作)
    std::mutex  subMutex_;
};
