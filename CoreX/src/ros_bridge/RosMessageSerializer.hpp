#pragma once
// ============================================================================
// RosMessageSerializer.hpp — 动态 ROS 消息二进制构造器
//
// 根据 YAML 字段描述（GenericActionFieldConfig）和 JSON 参数，动态构建
// ROS 消息的序列化二进制字节。支持嵌套消息。
//
// ROS 序列化格式即为 protobuf wire format:
//   wire_type 0 = varint  (int32, uint32, int64, uint64, bool, enum)
//   wire_type 1 = fixed64 (double, fixed64, sfixed64) — 8 bytes LE
//   wire_type 2 = length-delimited (string, bytes, nested messages)
//   wire_type 5 = fixed32 (float, fixed32, sfixed32) — 4 bytes LE
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace RosMessageSerializer {

// ---- 基础类型写入 ----

// 写入 varint 编码的 tag: (field_number << 3) | wire_type
void writeTag(std::string& buf, int fieldNumber, int wireType);

// 写入 varint 值
void writeVarint(std::string& buf, uint64_t value);

// 写入 double (wire_type=1, fixed64 LE)
void writeFloat64(std::string& buf, int fieldNumber, double value);

// 写入 float  (wire_type=5, fixed32 LE)
void writeFloat32(std::string& buf, int fieldNumber, float value);

// 写入 int32/uint32/bool (wire_type=0, varint)
void writeInt32(std::string& buf, int fieldNumber, int32_t value);
void writeUInt32(std::string& buf, int fieldNumber, uint32_t value);
void writeBool(std::string& buf, int fieldNumber, bool value);

// 写入 string (wire_type=2)
void writeString(std::string& buf, int fieldNumber, const std::string& value);

}  // namespace RosMessageSerializer
