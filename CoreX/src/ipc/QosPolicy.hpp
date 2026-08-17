#pragma once
#include <cstdint>

// ============================================================
// QoS 策略定义 — Pub/Sub 消息生命周期控制
// ============================================================

// QoS 策略枚举：决定新订阅者能否收到历史数据
enum class QoSType : uint8_t
{
    STREAM    = 0,  // 流式：新订阅者只能收到订阅之后发布的未来数据
    KEEP_LAST = 1,  // 保留最近 N 帧：新订阅者立即收到最后 depth 帧历史
    LATCH     = 2   // 锁存：保留最新一帧，后加入的订阅者立即收到（类似 ROS latched topic）
};

// QoS 配置参数，创建 Topic 时指定
struct QosPolicy
{
    QoSType  type  = QoSType::STREAM;
    uint32_t depth = 1;  // 仅在 KEEP_LAST 时有效，表示保留最近多少帧
};
