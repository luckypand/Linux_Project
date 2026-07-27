#pragma once
// ============================================================================
// RosMessageSerializer.hpp — 动态 ROS 消息二进制构造器
//
// 根据 YAML 字段描述（GenericActionFieldConfig）和 JSON 参数，动态构建
// ROS 消息的序列化二进制字节。支持嵌套消息。
//
// ★ ROS 1 序列化格式（非 Protobuf wire format）：
//   - 固定大小类型：纯数据，无 field tag
//     float64: 8 bytes LE
//     float32: 4 bytes LE
//     int32/uint32: 4 bytes LE（固定，非 varint）
//     bool: 1 byte (0/1)
//   - 字符串：4-byte LE uint32 长度 + UTF-8 数据（无 field tag）
//   - 嵌套消息：递归内联展开，无 field tag，无 length prefix
//   - 数组：4-byte LE uint32 计数 + 每个元素
//
// 关键区别：ROS 1 消息按定义顺序序列化，不需要 field_number tag。
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace RosMessageSerializer {

// ---- 基础类型写入（ROS 1 二进制格式，无 field tag）----

// 写入 4 字节 little-endian uint32（用于字符串/数组长度前缀）
void writeUint32LE(std::string& buf, uint32_t value);

// 写入 varint 值（保留用于整数类型）
void writeVarint(std::string& buf, uint64_t value);

// 写入 double (8 bytes LE, 无 tag)
void writeFloat64(std::string& buf, double value);

// 写入 float  (4 bytes LE, 无 tag)
void writeFloat32(std::string& buf, float value);

// 写入 int32 (4 bytes LE, 无 tag)
void writeInt32(std::string& buf, int32_t value);

// 写入 uint32 (4 bytes LE, 无 tag)
void writeUInt32(std::string& buf, uint32_t value);

// 写入 bool (1 byte, 无 tag)
void writeBool(std::string& buf, bool value);

// 写入 string (4-byte LE uint32 长度 + UTF-8 数据, 无 tag)
void writeString(std::string& buf, const std::string& value);

}  // namespace RosMessageSerializer
