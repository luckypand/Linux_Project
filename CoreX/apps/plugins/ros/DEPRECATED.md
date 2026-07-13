# DEPRECATED — ROS 独立插件

`control_plugin.cpp` 和 `telemetry_plugin.cpp` 已被 CoreX-ROS Bridge 模块 (`src/ros_bridge/`) 替代。

## 迁移指南

### 旧方式（独立 .so 插件）

```cpp
// 每个 Topic 手写一个 .so 插件，硬编码 topic 名称
class MotionControlServiceImpl : public RpcServiceAdapter { ... };
```

### 新方式（配置驱动）

```yaml
# corex_daemon.yaml
ros_bridge:
  topics:
    - ros_topic: "/cmd_vel"
      direction: "publish"
      rpc_service: "CoreX.rpc.MotionControl"
      rpc_method: "SetVelocity"
```

## 保留原因

这两个文件作为 **教学参考** 保留，展示如何手写 ROS 插件。实际生产环境请使用 `src/ros_bridge/` 模块。

## 功能对照

| 功能 | 旧插件 | 新 Bridge |
|------|--------|-----------|
| /cmd_vel 控制 | control_plugin.cpp | TopicBridge (publish) |
| /odom 遥测 | telemetry_plugin.cpp | TopicBridge (subscribe) |
| 其他 Topic | 需手写新 .so | 加一行 YAML 配置 |
| ROS Service | 不支持 | ServiceBridge |
| ROS Action | 不支持 | ActionBridge |
| 图像/点云 SHM | 不支持 | ShmImageTransporter |
