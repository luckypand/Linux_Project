// ============================================================================
// RosMessageSerializer.cpp — 动态 ROS 消息二进制构造器 实现
// ============================================================================

#include "RosMessageSerializer.hpp"
#include <stdexcept>

namespace RosMessageSerializer {

// ============================================================================
// Tag 编码
// ============================================================================
void writeTag(std::string& buf, int fieldNumber, int wireType)
{
    uint64_t tag = (static_cast<uint64_t>(fieldNumber) << 3) | static_cast<uint64_t>(wireType);
    writeVarint(buf, tag);
}

// ============================================================================
// Varint 编码
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
// Fixed64 (double) — 8 bytes little-endian
// ============================================================================
void writeFloat64(std::string& buf, int fieldNumber, double value)
{
    writeTag(buf, fieldNumber, 1);  // wire_type = 1 (fixed64)

    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; i++) {
        buf.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
    }
}

// ============================================================================
// Fixed32 (float) — 4 bytes little-endian
// ============================================================================
void writeFloat32(std::string& buf, int fieldNumber, float value)
{
    writeTag(buf, fieldNumber, 5);  // wire_type = 5 (fixed32)

    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 4; i++) {
        buf.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
    }
}

// ============================================================================
// Varint 类型 (int32/uint32/bool)
// ============================================================================
void writeInt32(std::string& buf, int fieldNumber, int32_t value)
{
    writeTag(buf, fieldNumber, 0);  // wire_type = 0 (varint)
    // Protobuf 中 int32 使用 varint 编码，负数需要转为 uint64 补码
    writeVarint(buf, static_cast<uint64_t>(static_cast<uint32_t>(value)));
}

void writeUInt32(std::string& buf, int fieldNumber, uint32_t value)
{
    writeTag(buf, fieldNumber, 0);
    writeVarint(buf, static_cast<uint64_t>(value));
}

void writeBool(std::string& buf, int fieldNumber, bool value)
{
    writeTag(buf, fieldNumber, 0);
    writeVarint(buf, value ? 1 : 0);
}

// ============================================================================
// Length-delimited (string)
// ============================================================================
void writeString(std::string& buf, int fieldNumber, const std::string& value)
{
    writeTag(buf, fieldNumber, 2);  // wire_type = 2 (length-delimited)
    writeVarint(buf, static_cast<uint64_t>(value.size()));
    buf.append(value);
}

}  // namespace RosMessageSerializer
