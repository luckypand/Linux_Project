# CoreX-ROS Bridge

CoreX-ROS Bridge 是 ROS 机器人生态与 CoreX 通信框架之间的协议网关，将 ROS 内部通信模型（Topic/Service/Action）抽象为 CoreX RPC 服务，实现机器人系统与云端通信平台的双向互通。

## 架构

```
云服务器 (AI/监控)
    │ CoreX RPC over TCP
    ▼
CoreX RPC Server
    │
    ▼
CoreX-ROS Bridge
    │
    ├── TopicBridge   (Pub/Sub ↔ RPC)
    ├── ServiceBridge (ROS Srv ↔ RPC)
    └── ActionBridge  (ROS Act ↔ RPC)
    │
    ▼
ROS Master / Topics / Services / Actions
    │
    ▼
Robot Hardware (Navigation / Sensors / Actuators)
```

## 模块结构

```
src/ros_bridge/
├── README.md                  # 本文件
├── RosCompat.hpp              # ROS 兼容层（无 ROS 时降级）
├── RosNodeManager.hpp/.cpp    # ROS 节点生命周期管理
├── BridgeConfig.hpp/.cpp      # YAML 配置解析
├── RosBridgeEngine.hpp/.cpp   # 核心引擎：组装 + 生命周期
├── DynamicServiceAdapter.hpp  # 动态 RPC Service 适配器基类
├── TopicBridge.hpp/.cpp       # Topic ↔ RPC 双向映射
├── ServiceBridge.hpp/.cpp     # ROS Service ↔ RPC
├── ActionBridge.hpp/.cpp      # ROS Action ↔ RPC
├── ShmImageTransporter.hpp/.cpp  # 高频图像共享内存快速通道
├── ShmTopicBus.hpp/.cpp         # ★ 共享内存 Topic 总线（同机替代 TCPROS）
└── RosProtobufConv.hpp        # ROS Message ↔ Protobuf 转换工具
```

## 使用方式

### 配置

在 `corex_daemon.yaml` 中添加 `ros_bridge` 段：

```yaml
ros_bridge:
  enabled: true
  node_name: "corex_ros_bridge"
  spinner_threads: 2

  topics:
    # ROS → CoreX (subscribe)
    - ros_topic: "/odom"
      ros_type: "nav_msgs/Odometry"
      direction: "subscribe"
      rpc_service: "CoreX.rpc.RobotTelemetry"
      rpc_method: "GetOdometry"
      rate_hz: 20

    # CoreX → ROS (publish)
    - ros_topic: "/cmd_vel"
      ros_type: "geometry_msgs/Twist"
      direction: "publish"
      rpc_service: "CoreX.rpc.MotionControl"
      rpc_method: "SetVelocity"

  services:
    - ros_service: "/gazebo/spawn_model"
      rpc_service: "CoreX.rpc.SimulationControl"
      rpc_method: "SpawnModel"

  actions:
    - ros_action: "/move_base"
      ros_action_type: "move_base_msgs/MoveBaseAction"
      rpc_service: "CoreX.rpc.Navigation"
      rpc_method_start: "StartNavigation"
      rpc_method_cancel: "CancelNavigation"
      rpc_method_feedback: "GetNavigationFeedback"
      rpc_method_result: "GetNavigationResult"
```

### 启动

```bash
# 1. 启动 ROS Master
roscore &

# 2. 启动 CoreXDaemon (自动加载 Bridge)
cd CoreX && ./build.sh
./build/CoreXDaemon --config apps/config/corex_daemon.yaml

# 3. 通过 RPC 客户端调用
./build/host_controller --host 127.0.0.1 --port 8080
```

### 嵌入式库模式

```cpp
#include "src/ros_bridge/RosBridgeEngine.hpp"

BridgeConfig cfg;
cfg.enabled = true;
cfg.topics.push_back({"/cmd_vel", "geometry_msgs/Twist",
                      TopicMappingConfig::Direction::Publish,
                      "MotionControl", "SetVelocity"});

RosBridgeEngine engine;
engine.initialize(cfg);
engine.start();

// 注册到 RpcServer
for (auto* adapter : engine.getServiceAdapters()) {
    rpcServer.registerService(adapter);
}
```

## 编译

Bridge 模块需要 ROS 环境（catkin + roscpp）。在无 ROS 的机器上编译时，Bridge 会被自动跳过。

```bash
# 在有 ROS 的机器上
cd CoreX && ./build.sh
# → 自动检测 ROS 并编译 lib/librosbridge.a

# 在无 ROS 的机器上
cd CoreX && ./build.sh
# → 提示 "ROS NOT found — skipping CoreX-ROS Bridge"
```

## 数据流向

### 方向 1: CoreX → ROS (云端控制机器人)

```
Cloud: RPC SetVelocity(linear=0.5, angular=0.2)
  ↓
CoreX RpcServer → dispatchTable["MotionControl"]
  ↓
TopicBridge::handlePublish()
  ↓
geometry_msgs::Twist 序列化 → Publisher::publish()
  ↓
/cmd_vel topic → 机器人运动控制器
```

### 方向 2: ROS → CoreX (机器人状态上传)

```
ROS /odom 发布 (100Hz)
  ↓
TopicBridge::onRosMessage() → 缓存最新数据
  ↓
Cloud: RPC GetOdometry()
  ↓
TopicBridge::handleGetCached() → 返回缓存数据
  ↓
Cloud 收到机器人位姿/速度
```

### 方向 3: 同机 SHM 直通 (CoreX ⇄ CoreX, 替代 TCPROS)

启用 `use_shm_topic: true` 后，TopicBridge 在 publish/subscribe 时同步写入共享内存环形缓冲：

```
CoreX Client A (Publisher)          CoreX Client B (Subscriber)
  │                                     │
  │ ShmTopicBus::subscribe()            │ ShmTopicBus::subscribe()
  │   → 获取 eventfd                    │   → 注册到 subscriber_fds[]
  │                                     │
  │ ShmTopicBus::publish(data, size)    │
  │   → write_seq++                     │
  │   → memcpy to slot[idx]             │
  │   → slot[idx].seq_num.store(release) │
  │   → write(eventfd, 1) notify ──────→ poll(eventfd) 唤醒
  │                                     │
  │                                     │ ShmTopicBus::tryRecv()
  │                                     │   → scan slots for max seq_num
  │                                     │   → 返回最新消息
```

**与 TCPROS 的关系**：SHM 和 TCPROS **并行运行**。SHM 服务本机 CoreX 客户端，TCPROS 服务标准 ROS 节点和跨机订阅者。

## 性能特性

| 特性 | 描述 |
|------|------|
| Topic 延迟 (RPC 查询) | < 100μs（缓存读取 + 序列化） |
| 图像通道 (SHM) | < 1ms（三缓冲共享内存） |
| **同机 Topic (SHM)** | **< 10μs（序列号环形缓冲, 替代 TCPROS 的 ~50-200μs）** |
| Service 调用 | 取决于 ROS Service 响应时间 |
| Action 轮询 | 10ms 间隔查询 Feedback |
| 内存占用 | < 50MB（不含图像缓冲） |

## 与 rosbridge 的关系

CoreX-ROS Bridge 与官方 [rosbridge_suite](https://github.com/RobotWebTools/rosbridge_suite) 互补：

| 特性 | CoreX-ROS Bridge | rosbridge |
|------|-----------------|-----------|
| 协议 | Protobuf (二进制) | JSON (文本) |
| 传输 | TCP + SHM | WebSocket |
| 性能 | 高（二进制 + SHM） | 中（JSON + WS） |
| 客户端 | C++ / Python (protobuf) | 任意语言 (JSON) |
| 适用场景 | 高性能本地通信 | Web 前端 / 跨语言 |
