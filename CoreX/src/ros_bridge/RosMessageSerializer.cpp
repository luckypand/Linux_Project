// ============================================================================
// RosMessageSerializer.cpp — 动态 ROS 消息二进制构造器 实现
//
// ★ 使用 ROS 1 原生二进制格式，非 Protobuf wire format
//   - 所有类型：无 field tag
//   - 固定大小字段：纯数据 LE
//   - 字符串/数组：4-byte LE uint32 长度前缀
//   - 嵌套消息：递归内联，无 length prefix
// ============================================================================

#include "RosMessageSerializer.hpp"
#include <stdexcept>

namespace RosMessageSerializer {

// ============================================================================
// 4-byte Little-Endian uint32（用于字符串/数组长度前缀）
// ============================================================================
void writeUint32LE(std::string& buf, uint32_t value)
{
    buf.push_back(static_cast<char>(value & 0xFF));
    buf.push_back(static_cast<char>((value >> 8) & 0xFF));
    buf.push_back(static_cast<char>((value >> 16) & 0xFF));
    buf.push_back(static_cast<char>((value >> 24) & 0xFF));
}

// ============================================================================
// Varint 编码（保留，用于整数类型编码）
// ============================================================================
void writeVarint(std::string& buf, uint64_t value)
{
    while (value > 0x7F) {
        buf.push_back(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buf.push_back(static_cast<char>(value & 0x7F));
}

// ============================================================================
// Fixed64 (double) — 8 bytes little-endian，无 tag
// ============================================================================
void writeFloat64(std::string& buf, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; i++) {
        buf.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
    }
}

// ============================================================================
// Fixed32 (float) — 4 bytes little-endian，无 tag
// ============================================================================
void writeFloat32(std::string& buf, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 4; i++) {
        buf.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
    }
}

// ============================================================================
// int32 — 4 bytes little-endian，无 tag
// ============================================================================
void writeInt32(std::string& buf, int32_t value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 4; i++) {
        buf.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
    }
}

// ============================================================================
// uint32 — 4 bytes little-endian，无 tag
// ============================================================================
void writeUInt32(std::string& buf, uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        buf.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
    }
}

// ============================================================================
// bool — 1 byte (0/1)，无 tag
// ============================================================================
void writeBool(std::string& buf, bool value)
{
    buf.push_back(static_cast<char>(value ? 1 : 0));
}

// ============================================================================
// string — 4-byte LE uint32 长度 + UTF-8 数据，无 tag
// ============================================================================
void writeString(std::string& buf, const std::string& value)
{
    writeUint32LE(buf, static_cast<uint32_t>(value.size()));
    buf.append(value);
}

}  // namespace RosMessageSerializer
