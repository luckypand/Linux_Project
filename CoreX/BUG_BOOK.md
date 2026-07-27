# BUG_BOOK.md — CoreX 项目 Bug 记录

## Bug #1: GenericActionBridge ROS 消息序列化格式错误

**日期**: 2026-07-27

**严重程度**: 高（导致 ROS 端接收到的数据完全错误）

**症状**:
Python RPC 客户端发送 `action smooth_vel linear_x=0.2`，ROS 端 `rostopic echo /smooth_vel` 收到垃圾数据：
```
linear:  x: -2.35e-185, y: 1.43e-315, z: 5.30e-313
angular: x: 8.40e-265, y: 0.0,       z: 8.4e-323
```

**根因**:
`RosMessageSerializer` 和 `GenericActionBridge::buildMessageBytes` 使用 **Protobuf wire format**（带 field tag + varint length prefix）来构造 ROS 消息字节，但 **ROS 1 消息序列化格式是纯数据顺序排列**，不使用 field tag。

两种格式的根本区别：

| 特性 | Protobuf Wire Format | ROS 1 Binary Format |
|------|---------------------|---------------------|
| Field tag | varint(field_number << 3 \| wire_type) | **无** |
| 嵌套消息 | length-delimited (varint length) | **直接内联展开** |
| float64 | tag + 8 bytes LE | **仅 8 bytes LE** |
| 字符串长度 | varint | **4 bytes LE uint32** |

**修复**:
1. `RosMessageSerializer.hpp/cpp`: 重写所有函数 — 移除 `writeTag`，所有原始类型不再写 tag，字符串长度改为 4-byte LE uint32
2. `GenericActionBridge.cpp::buildMessageBytes()`: 嵌套消息直接 `buf.append(nested)`，移除 `writeTag` + `writeVarint(length)` 调用

**修复后验证**:
- 4 个测试用例全部通过：序列化字节与 ROS 原生 `ros::serialization::serialize()` 输出完全一致
- ROS 反序列化 roundtrip 验证正确

**修改文件**:
- `src/ros_bridge/RosMessageSerializer.hpp`
- `src/ros_bridge/RosMessageSerializer.cpp`
- `src/ros_bridge/GenericActionBridge.cpp`
