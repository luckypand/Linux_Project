# CoreX 拓展模块

## 拓展模块架构示意图

```
                云服务器
         AI识别/任务调度系统
                  |
            CoreX RPC Client
                  |
                TCP/IP
                  |
          CoreX RPC Server
                  |
         CoreX-ROS Bridge   ← [已完成] 2026-07-10
             ↑          ↓
       ROS Topic    ROS Service
       ROS Action
             ↑          ↓
      Navigation / MoveIt
             ↑
         Robot Hardware
```

## 已完成模块

### CoreX-ROS Bridge (`src/ros_bridge/`)

**状态**: ✅ 已完成

将 ROS 内部通信模型（Topic/Service/Action）抽象为 CoreX RPC 服务，实现机器人系统与云端双向互通。支持通过 `RpcMessage.robot_id` 字段实现多机器人定向控制。

| 组件 | 文件 | 功能 |
|------|------|------|
| RosBridgeEngine | `RosBridgeEngine.hpp/.cpp` | 核心引擎：配置→适配器组装→生命周期，含 robot_id 设置 |
| TopicBridge | `TopicBridge.hpp/.cpp` | Topic ↔ RPC 双向映射（subscribe 缓存 / publish 转发） |
| ServiceBridge | `ServiceBridge.hpp/.cpp` | ROS Service ↔ RPC（通用 bytes 透传） |
| ActionBridge | `ActionBridge.hpp/.cpp` | ROS Action ↔ RPC（Goal/Feedback/Result/Cancel） |
| ShmTopicBus | `ShmTopicBus.hpp/.cpp` | ★ 同机 SHM Topic 总线（替代 TCPROS, 延迟 < 10μs） |
| ShmImageTransporter | `ShmImageTransporter.hpp/.cpp` | 高频图像三缓冲共享内存通道 |
| BridgeConfig | `BridgeConfig.hpp/.cpp` | YAML 配置驱动的 Topic/Service/Action 映射 + robot_id |
| RosNodeManager | `RosNodeManager.hpp/.cpp` | ROS 节点生命周期管理 |

### Proto 扩展

| 文件 | 内容 |
|------|------|
| `proto/robot_service.proto` | RobotTelemetry, MotionControl, SimulationControl, Navigation 共 4 个 Service, 30+ Message |
| `proto/ros_messages.proto` | JointState, Image, LaserScan, PointCloud2, TFMessage, Imu, BatteryState 等 ROS 消息类型 |

### 测试

- `tests/test_ros_bridge.cpp` — 11 项单元测试（全部通过）
  - 6 项基础测试（配置解析、适配器）
  - 5 项 ShmTopicBus 测试（读写、多订阅者、环形覆盖、eventfd 唤醒、配置解析）

## 待办

- [ ] ROS 2 (rclcpp) 支持
- [ ] WebSocket rosbridge 协议兼容
- [ ] Action 完整实现（当前为基础框架，待接入 actionlib）
- [ ] 插件热加载（SIGHUP）机制
- [ ] ROS Master 断线重连
