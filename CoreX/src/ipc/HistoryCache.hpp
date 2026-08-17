#pragma once
#include <cstdint>
#include <vector>
#include "QosPolicy.hpp"

// ============================================================
// HistoryCache — 历史 Block ID 缓存（进程本地内存）
// ============================================================
//
// QoS 语义：
//   STREAM:    不缓存任何历史，新订阅者只收到未来数据
//   KEEP_LAST: 缓存最近 depth 个 Block ID（环形覆盖，保留最旧的被淘汰）
//   LATCH:     缓存最新 1 个 Block ID（新值覆盖旧值）
//
// 全部数据在进程本地内存，不需要跨进程共享。
// 只有生产者（TopicChannel）写入和读取。

class HistoryCache
{
public:
    HistoryCache() = default;
    ~HistoryCache() = default;

    // 根据 QoS 初始化缓存
    void init(QoSType qos, uint32_t depth = 1);

    // 生产者发布新数据时调用
    // 返回被淘汰的 Block ID（需要调用方 release），UINT32_MAX 表示无淘汰
    uint32_t push(uint32_t block_id);

    // 新订阅者加入时调用：获取历史 Block ID（按时间从旧到新）
    std::vector<uint32_t> getHistory() const;

    // 查询
    QoSType  qosType() const { return qos_; }
    uint32_t depth()   const { return depth_; }
    uint32_t size()    const;  // 当前缓存中的元素数量

private:
    QoSType              qos_        = QoSType::STREAM;
    uint32_t             depth_      = 1;
    uint32_t             count_      = 0;    // 已缓存的元素数量（≤ depth_）
    uint32_t             write_pos_  = 0;    // 环形写指针
    std::vector<uint32_t> buffer_;           // history_[depth_] 环形缓冲区
};
