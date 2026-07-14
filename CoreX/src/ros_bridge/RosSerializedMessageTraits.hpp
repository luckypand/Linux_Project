#pragma once
// ============================================================================
// RosSerializedMessageTraits.hpp — ros::SerializedMessage 的 traits 特化
//
// ROS noetic 中 ros::SerializedMessage 缺少标准的 message_traits /
// service_traits / serialization 成员函数。本文件为其提供模板特化，
// 使 SerializedMessage 可用于 ros::Publisher::publish()、
// ros::Subscriber::subscribe() 和 ros::ServiceClient::call() 等模板 API。
//
// 使用方式：在需要 publish/subscribe/call SerializedMessage 的 .cpp 中
//           #include 本头文件即可（需在 ROS 头文件之后）。
// ============================================================================

#include <ros/serialized_message.h>
#include <ros/message_traits.h>
#include <ros/service_traits.h>
#include <ros/serialization.h>

namespace ros
{

// ============================================================================
// message_traits 特化（Publisher/Subscriber 模板需要）
// ============================================================================
namespace message_traits
{

template<>
struct MD5Sum<ros::SerializedMessage>
{
    static const char* value() { return "*"; }
    static const char* value(const ros::SerializedMessage&) { return "*"; }
};

template<>
struct DataType<ros::SerializedMessage>
{
    static const char* value() { return "*"; }
    static const char* value(const ros::SerializedMessage&) { return "*"; }
};

template<>
struct Definition<ros::SerializedMessage>
{
    static const char* value() { return ""; }
    static const char* value(const ros::SerializedMessage&) { return ""; }
};

}  // namespace message_traits

// ============================================================================
// service_traits 特化（ServiceClient 模板需要）
// ============================================================================
namespace service_traits
{

template<>
struct MD5Sum<ros::SerializedMessage>
{
    static const char* value() { return "*"; }
    static const char* value(const ros::SerializedMessage&) { return "*"; }
};

template<>
struct DataType<ros::SerializedMessage>
{
    static const char* value() { return "*"; }
    static const char* value(const ros::SerializedMessage&) { return "*"; }
};

}  // namespace service_traits

// ============================================================================
// serialization 特化（publish/subscribe 模板需要）
// ============================================================================
namespace serialization
{

template<>
struct Serializer<ros::SerializedMessage>
{
    // ★ 写入：SerializedMessage 已是序列化后的数据，直接拷贝
    template<typename Stream>
    inline static void write(Stream& stream, const ros::SerializedMessage& m)
    {
        if (m.num_bytes > 0 && m.message_start) {
            memcpy(stream.advance(m.num_bytes), m.message_start, m.num_bytes);
        }
    }

    // ★ 读取：从流中重建 SerializedMessage
    template<typename Stream>
    inline static void read(Stream& stream, ros::SerializedMessage& m)
    {
        // SerializedMessage 的反序列化场景：从中读取原始字节
        // 通常由 SubscriptionCallbackHelper 处理，这里提供最小实现
        uint32_t len = stream.getLength();
        m.num_bytes = len;
        m.buf.reset(new uint8_t[len]);
        m.message_start = m.buf.get();
        if (len > 0) {
            memcpy(m.buf.get(), stream.advance(len), len);
        }
    }

    inline static uint32_t serializedLength(const ros::SerializedMessage& m)
    {
        return static_cast<uint32_t>(m.num_bytes);
    }
};

}  // namespace serialization

}  // namespace ros
