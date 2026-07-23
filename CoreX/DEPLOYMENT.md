# CoreX 部署与使用手册

> 版本: v1.0.0 | 最后更新: 2026-07-10 | 适用于 CoreX 完整功能集

---

## 目录

- [一、项目概览](#一项目概览)
- [二、环境要求与依赖安装](#二环境要求与依赖安装)
- [三、快速开始（5 分钟）](#三快速开始5-分钟)
- [四、守护进程模式详解](#四守护进程模式详解)
- [五、嵌入式库模式详解](#五嵌入式库模式详解)
- [六、ROS Bridge 模式详解](#六ros-bridge-模式详解)
- [七、ShmTopicBus 同机高性能通信](#七shmtopicbus-同机高性能通信)
- [八、编写业务插件](#八编写业务插件)
- [九、客户端开发指南](#九客户端开发指南)
- [十、生产环境部署](#十生产环境部署)
- [十一、性能调优](#十一性能调优)
- [十二、故障排查](#十二故障排查)
- [十三、API 参考](#十三api-参考)

---

## 一、项目概览

### 1.1 CoreX 是什么？

CoreX 是一个基于 Reactor 模式的 Linux C++17 网络库与 RPC 框架，专为**机器人/自动驾驶**场景设计。它提供三种使用模式：

```
┌─────────────────────────────────────────────────────────────────┐
│                        CoreX 功能全景                            │
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ 守护进程模式   │  │ 嵌入式库模式   │  │  ROS Bridge 模式 ★    │  │
│  │              │  │              │  │                      │  │
│  │ CoreXDaemon  │  │ librpc.a     │  │ RosBridgeEngine      │  │
│  │ + 插件 .so   │  │ 编译进工程    │  │ + TopicBridge        │  │
│  │ + YAML 配置   │  │ 直接调 API    │  │ + ServiceBridge      │  │
│  │              │  │              │  │ + ActionBridge       │  │
│  └──────┬───────┘  └──────┬───────┘  │ + ShmTopicBus (SHM)   │  │
│         │                 │          └──────────┬───────────┘  │
│         │    ┌────────────┴──────────┐          │              │
│         └───→│  RPC 框架层            │←────────┘              │
│              │  RpcServer + Codec     │                        │
│              │  + ServiceAdapter      │                        │
│              ├───────────────────────┤                        │
│              │  网络引擎层             │                        │
│              │  TcpServer + EventLoop │                        │
│              ├───────────────────────┤                        │
│              │  基础设施层             │                        │
│              │  IPC (SHM RingBuffer)  │                        │
│              │  Log (异步日志)         │                        │
│              │  Timer (HeapTimer)     │                        │
│              └───────────────────────┘                        │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 三种模式速览

| 模式 | 适用场景 | 一句话描述 |
|------|---------|-----------|
| **守护进程模式** | 生产环境 RPC 中间件 | 独立进程，YAML 配置，插件 .so 热加载 |
| **嵌入式库模式** | C++ 工程内嵌集成 | 链接 `librpc.a`，编译时注册服务 |
| **ROS Bridge 模式** | 机器人/自动驾驶 | 将 ROS Topic/Service/Action 映射为 RPC，云端↔机器人互通 |

### 1.3 目录结构

```
CoreX/
├── README.md                         # 项目说明
├── DEPLOYMENT.md                     # ★ 本文件 — 部署与使用手册
├── CLAUDE.md                         # AI 辅助开发指南
├── Extension.md                      # 拓展模块架构设计
├── build.sh                          # 一键编译脚本
├── CMakeLists.txt                    # CMake 构建
│
├── proto/                            # Protobuf 定义 + 生成代码
│   ├── rpc_message.proto             # RPC 信封 (REQUEST/RESPONSE/ERROR)
│   ├── math_service.proto            # 示例: MathService (Add/Sub)
│   ├── robot_service.proto           # ★ 机器人 RPC 服务定义
│   └── ros_messages.proto            # ★ ROS 消息 Protobuf 等价定义
│
├── src/                              # 核心源码
│   ├── net/                          # Reactor 网络引擎 (EventLoop, TcpServer, ...)
│   ├── rpc/                          # RPC 框架 (RpcServer, RpcServiceAdapter, ...)
│   ├── ipc/                          # 共享内存 IPC (ShmRingBuffer, IpcEndpoint, ...)
│   ├── ros_bridge/                   # ★ ROS Bridge 协议网关
│   │   ├── RosBridgeEngine.hpp/.cpp  #   核心引擎
│   │   ├── TopicBridge.hpp/.cpp      #   Topic ↔ RPC
│   │   ├── ServiceBridge.hpp/.cpp    #   ROS Service ↔ RPC
│   │   ├── ActionBridge.hpp/.cpp     #   ROS Action ↔ RPC
│   │   ├── ShmTopicBus.hpp/.cpp      #   同机 SHM Topic 总线
│   │   ├── ShmImageTransporter.hpp/.cpp # 图像 SHM 快速通道
│   │   ├── BridgeConfig.hpp/.cpp     #   YAML 配置解析
│   │   └── RosNodeManager.hpp/.cpp   #   ROS 节点管理
│   └── log/                          # 异步日志
│
├── apps/                             # 守护进程封装层
│   ├── daemon/                       # CoreXDaemon 源码
│   ├── config/corex_daemon.yaml      # 默认配置模板
│   ├── examples/
│   │   ├── math_plugin/              # 业务插件示例
│   │   └── robot_controller/         # 机器人遥控面板
│   ├── plugins/ros/                  # ROS 插件（旧版，已被 src/ros_bridge 取代）
│   └── USAGE.md                      # 使用文档
│
└── tests/                            # 测试
    ├── test_ros_bridge.cpp           # ★ ROS Bridge 单元测试 (11 项)
    ├── test_rpc_benchmark.cpp        # RPC 压测
    ├── test_net_integration.cpp      # 网络库集成测试
    ├── Test_EchoSever.cpp            # Echo 压测
    └── timeout_server_test.cpp       # 超时测试
```

---

## 二、环境要求与依赖安装

### 2.1 基础依赖（所有模式必需）

| 依赖 | 最低版本 | 用途 |
|------|:------:|------|
| Linux Kernel | 4.x+ | epoll, eventfd, shared memory |
| GCC / Clang | 8.0+ (C++17) | 编译 |
| CMake | 3.8+ | 构建系统 |
| Protobuf | 3.6.1+ | RPC 序列化 |
| yaml-cpp | 0.6+ | YAML 配置解析 |
| OpenSSL | 1.1+ | IPC 测试用 MD5 |
| pthreads | 任意 | 多线程 |
| librt | 任意 | POSIX 共享内存 |

### 2.2 安装基础依赖（Ubuntu 18.04/20.04）

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    protobuf-compiler \
    libprotobuf-dev \
    libyaml-cpp-dev \
    libssl-dev
```

### 2.3 ROS 依赖（仅 ROS Bridge 模式）

ROS Bridge 模块需要 ROS 1 (Melodic 或 Noetic)：

```bash
# Ubuntu 18.04 + ROS Melodic
sudo sh -c 'echo "deb http://packages.ros.org/ros/ubuntu $(lsb_release -sc) main" > /etc/apt/sources.list.d/ros-latest.list'
sudo apt-key adv --keyserver 'hkp://keyserver.ubuntu.com:80' --recv-key C1CF6E31E6BADE8868B172B4F42ED6FBAB17C654
sudo apt update
sudo apt install -y \
    ros-melodic-roscpp \
    ros-melodic-nav-msgs \
    ros-melodic-geometry-msgs \
    ros-melodic-sensor-msgs \
    ros-melodic-tf2-msgs
```

### 2.4 验证安装

```bash
# 检查基础工具
cmake --version          # ≥ 3.8
protoc --version         # ≥ 3.6
g++ --version            # ≥ 8.0

# 检查 ROS (可选)
roscore --version 2>/dev/null || echo "ROS not installed (OK if not using Bridge)"
```

---

## 三、快速开始（5 分钟）

### 3.1 克隆与编译

```bash
cd /root/Cplus/CoreX

# 全量编译 (不含 ROS Bridge，因为它需要 ROS 环境)
./build.sh

# 查看编译产物
ls build/
# CoreXDaemon           — 守护进程可执行文件
# test_ros_bridge        — ROS Bridge 单元测试
# test_rpc_benchmark     — RPC 压测工具
# test_echo_server       — Echo 压测工具
# host_controller        — 机器人遥控面板
# plugins/               — 示例插件 .so 文件
```

### 3.2 启动守护进程（体验 RPC）

```bash
# 1. 前台启动
./build/CoreXDaemon --config apps/config/corex_daemon.yaml

# 看到以下 banner 表示成功:
# ╔══════════════════════════════════════════════╗
# ║        CoreX RPC Daemon v1.0.0               ║
# ╠══════════════════════════════════════════════╣
# ║  TCP  : 0.0.0.0:8080                        ║
# ║  IPC  : /corex_rpc_ipc                      ║
# ║  Services: 1 loaded                         ║
# ║    - CoreX.rpc.MathService                  ║
# ╚══════════════════════════════════════════════╝

# 2. 新开终端，测试 RPC 调用
python3 apps/examples/python_client/rpc_client.py add --a 3 --b 5
# → Result: 8  Latency: 0.28 ms

python3 apps/examples/python_client/rpc_client.py sub --a 10 --b 3
# → Result: 7  Latency: 0.27 ms
```

### 3.3 体验 ROS Bridge（需要 ROS 环境）

```bash
# 终端 1: 启动 ROS Master
roscore &

# 终端 2: 编辑配置启用 Bridge
# 修改 apps/config/corex_daemon.yaml:
#   ros_bridge.enabled: true

# 终端 2: 启动 CoreXDaemon
./build/CoreXDaemon --config apps/config/corex_daemon.yaml

# 终端 3: 使用遥控面板
./build/host_controller --host 127.0.0.1 --port 8080
# 交互命令: f 0.5 (前进)  b 0.3 (后退)  l 0.5 (左转)  s (急停)  q (退出)
```

---

## 四、守护进程模式详解

### 4.1 架构

```
┌──────────────────────────────────────────────────────┐
│                  CoreXDaemon 进程                      │
│                                                      │
│  main.cpp → CoreXDaemon                              │
│    │           │                                     │
│    │           ├─ ConfigManager (corex_daemon.yaml)  │
│    │           ├─ PluginLoader (dlopen .so)          │
│    │           ├─ RosBridgeEngine (ROS Bridge)       │
│    │           └─ SignalHandler (SIGTERM/SIGINT)     │
│    │                                                 │
│    ▼                                                 │
│  RpcServer (TcpServer + RpcCodec)                    │
│    │                                                 │
│    ├── TCP :8080  ← 多语言客户端 (Python, C++, ...)   │
│    └── IPC (SHM)  ← 本地极速客户端 (同机 C++)         │
│                                                      │
│  注册的业务服务:                                       │
│    - MathService (插件 .so 加载)                       │
│    - RobotTelemetry (ROS Bridge, YAML 配置)           │
│    - MotionControl  (ROS Bridge, YAML 配置)           │
└──────────────────────────────────────────────────────┘
```

### 4.2 配置文件详解

完整配置文件位于 `apps/config/corex_daemon.yaml`：

```yaml
# ============================================================
# 服务器配置
# ============================================================
server:
  name: "CoreXDaemon"       # 服务器名称（日志和连接标识）
  host: "0.0.0.0"           # 监听地址（0.0.0.0 = 所有网卡）
  port: 8080                # TCP 监听端口
  worker_threads: 4         # IO 工作线程数（子 Reactor 数量）

# ============================================================
# IPC 快速通道（本地共享内存，低延迟）
# ============================================================
ipc:
  enabled: true                       # 是否启用
  shm_name: "/corex_rpc_ipc"          # 共享内存段名称

# ============================================================
# 插件配置
# ============================================================
plugins:
  directory: "./plugins"              # .so 搜索目录
  autoload: true                      # 启动时自动加载

# ============================================================
# 日志配置（需编译时 ENABLE_LOG=ON）
# ============================================================
logging:
  level: "INFO"                       # DEBUG | INFO | WARN | ERROR | FATAL
  path: "./log"

# ============================================================
# 监控配置
# ============================================================
monitoring:
  health_check_port: 9090             # HTTP 健康检查（预留）
  latency_stats: false                # RPC 延迟统计（预留）

# ============================================================
# ROS Bridge 配置（需要 ROS 环境）
# ============================================================
ros_bridge:
  enabled: false                      # 默认关闭
  node_name: "corex_ros_bridge"
  spinner_threads: 2
  robot_id: "robot_001"               # ★ 本机机器人唯一标识（云端通过此 ID 定向控制）

  topics:
    # ROS → CoreX (subscribe)
    - ros_topic: "/odom"
      ros_type: "nav_msgs/Odometry"
      direction: "subscribe"
      rpc_service: "CoreX.rpc.RobotTelemetry"
      rpc_method: "GetOdometry"
      use_shm_topic: true            # ★ 同机走 SHM 加速
      rate_hz: 20

    # CoreX → ROS (publish)
    - ros_topic: "/cmd_vel"
      ros_type: "geometry_msgs/Twist"
      direction: "publish"
      rpc_service: "CoreX.rpc.MotionControl"
      rpc_method: "SetVelocity"
      use_shm_topic: true

  services:
    - ros_service: "/gazebo/spawn_model"
      rpc_service: "CoreX.rpc.SimulationControl"
      rpc_method: "SpawnModel"
      timeout_ms: 5000

  actions:
    - ros_action: "/move_base"
      ros_action_type: "move_base_msgs/MoveBaseAction"
      rpc_service: "CoreX.rpc.Navigation"
      rpc_method_start: "StartNavigation"
      rpc_method_cancel: "CancelNavigation"
      rpc_method_feedback: "GetNavigationFeedback"
      rpc_method_result: "GetNavigationResult"
      timeout_ms: 30000
```

### 4.3 启动选项

```bash
# 前台运行（查看日志和 banner）
./build/CoreXDaemon --config apps/config/corex_daemon.yaml

# 后台守护进程模式
./build/CoreXDaemon --config apps/config/corex_daemon.yaml --daemon

# 覆盖端口
./build/CoreXDaemon --config apps/config/corex_daemon.yaml --port 9090

# 覆盖监听地址
./build/CoreXDaemon --config apps/config/corex_daemon.yaml --host 192.168.1.100

# 查看版本
./build/CoreXDaemon --version

# 查看帮助
./build/CoreXDaemon --help
```

### 4.4 优雅关闭

```bash
# 推荐: SIGTERM 优雅关闭
kill -TERM $(pidof CoreXDaemon)

# 关闭流程:
# 1. 停止 accept 新连接
# 2. 等待进行中请求完成 (30s 超时)
# 3. 排空 IPC RingBuffer
# 4. 清理 SHM → 卸载插件 → 退出

# 强制关闭
kill -KILL $(pidof CoreXDaemon)
```

---

## 五、嵌入式库模式详解

### 5.1 适用场景

- 在已有 C++ 工程中嵌入 RPC 服务
- 不需要独立进程
- 追求最低延迟（函数调用级）

### 5.2 CMake 集成

```cmake
# 方式 1: add_subdirectory
add_subdirectory(path/to/CoreX)
target_link_libraries(your_app rpc)

# 方式 2: find_package (安装后)
find_package(CoreX REQUIRED)
target_link_libraries(your_app CoreX::rpc)

# 方式 3: 手动链接
target_link_libraries(your_app
    -Lpath/to/CoreX/build -lrpc
    -lprotobuf -lpthread -lrt)
```

### 5.3 最小示例

```cpp
#include "src/net/EventLoop.hpp"
#include "src/rpc/RpcServer.hpp"
#include "src/rpc/RpcServiceAdapter.hpp"
#include "proto/math_service.pb.h"

// 1. 继承 RpcServiceAdapter 实现业务逻辑
class MyMathService : public RpcServiceAdapter
{
public:
    MyMathService()
        : RpcServiceAdapter(
              CoreX::rpc::MathRequest::descriptor()
                  ->file()->FindServiceByName("MathService"))
    {
        registerHandler("Add",
            [](Message* req, Message* resp) {
                auto* r = static_cast<MathRequest*>(req);
                auto* s = static_cast<MathResponse*>(resp);
                s->set_result(r->a() + r->b());
                s->set_success(true);
            });
    }
};

int main()
{
    EventLoop loop;
    MyMathService mathSvc;

    RpcServer server(&loop, "0.0.0.0", 8080, "MyApp");
    server.registerService(&mathSvc);
    server.start();

    loop.loop();  // 阻塞, 进入事件循环
    return 0;
}
```

### 5.4 启用 IPC Fast-Path

```cpp
// 嵌入式模式同样可以使用共享内存 IPC 加速
RpcServer server(&loop, "0.0.0.0", 8080, "MyApp");
server.enableIpc("/my_app_ipc");  // 启用 SHM 快速通道
```

---

## 六、ROS Bridge 模式详解

### 6.1 架构与数据流

```
                        云服务器 (Cloud)
                        AI 识别 / 任务调度
                              │
                     CoreX RPC over TCP
                     (TLV + Protobuf)
                              │
                    ┌─────────▼─────────┐
                    │   CoreXDaemon      │
                    │   (机器人侧)        │
                    │                    │
                    │  ┌──────────────┐  │
                    │  │ RosBridgeEngine│ │
                    │  │              │  │
                    │  │ TopicBridge  │  │ ← 双向 Topic 映射
                    │  │  /cmd_vel ──→│──┼──→ ROS Publisher  ──→ 机器人运动
                    │  │  /odom    ←──│──┼──← ROS Subscriber ←── 机器人传感器
                    │  │              │  │
                    │  │ ServiceBridge│  │ ← ROS Service 调用
                    │  │  /gazebo/... │  │
                    │  │              │  │
                    │  │ ActionBridge │  │ ← ROS Action 调用
                    │  │  /move_base  │  │
                    │  └──────────────┘  │
                    └───────────────────┘
                              │
                    ROS Master + Topics
                              │
                    机器人硬件 (Navigation / Sensors)
```

### 6.2 ★ Robot ID 多机器人定向控制

在多机器人场景下，云端可通过 `RpcMessage.robot_id` 字段指定目标机器人，实现精准定向控制。

**工作原理：**

```
云端 → RPC 请求 {robot_id: "robot_002", service: "MotionControl", method: "SetVelocity"}
  ↓
机器人 A (robot_id="robot_001") → robot_id 不匹配 → 拒绝 (INVALID_REQUEST)
机器人 B (robot_id="robot_002") → robot_id 匹配 → 执行指令
机器人 C (robot_id 未设置/空)   → 空=不校验 → 执行指令 (兼容模式)
```

**配置方式：**

```yaml
# 每台机器人配置唯一的 robot_id
ros_bridge:
  enabled: true
  robot_id: "robot_001"    # ★ 修改此处为每台机器人分配唯一 ID
```

**协议层：** `RpcMessage` Proto 字段 10: `string robot_id = 10`（空=任意机器人）。

**过滤逻辑：** [RpcServer.cpp:96-112](src/rpc/RpcServer.cpp#L96-L112) — 仅在 `localRobotId_` 和 `rpcMsg.robot_id()` 均非空时才校验，任一为空则放行（向后兼容）。

**Python 客户端示例：**
```bash
# 定向控制 robot_002
python3 rpc_client.py --host <ip> --port 8080 --robot_id robot_002 motion SetVelocity --linear_x 0.5
```

**⚠️ 注意：** C++ `RpcClient` ([RpcClient.hpp](apps/examples/robot_controller/RpcClient.hpp)) 当前未支持 robot_id 设置，仅 Python 客户端支持。如需在 C++ 客户端使用，需在 `call()` 方法中添加 `rpcMsg.set_robot_id()` 调用。

### 6.3 配置驱动 vs 手写插件

**旧方式 (已弃用)**：每个 Topic 写一个 `.so` 插件

```cpp
// control_plugin.cpp — 硬编码 /cmd_vel
class MotionControlServiceImpl : public RpcServiceAdapter {
    MotionControlServiceImpl() {
        cmdVelPub_ = nh.advertise<geometry_msgs::Twist>("/cmd_vel", 1);
        registerHandler("SetVelocity", ...);
    }
};
```

**新方式 (推荐)**：YAML 一行搞定

```yaml
ros_bridge:
  topics:
    - ros_topic: "/cmd_vel"
      direction: "publish"
      rpc_service: "CoreX.rpc.MotionControl"
      rpc_method: "SetVelocity"
```

### 6.4 Topic 映射详解

#### 方向 A: ROS → CoreX (subscribe)

Bridge 作为 ROS 订阅者，缓存数据供云端 RPC 查询。

```
ROS /odom 发布 (100Hz)
    ↓ ros::Subscriber 回调
    ↓ 缓存最新数据 (mutex, 零阻塞读)
    ↓ [可选: 同时推入 ShmTopicBus, 供同机 CoreX 消费者]
    ↓
Cloud: RPC GetOdometry()
    ↓ 返回缓存的 OdometryResponse
```

```yaml
topics:
  - ros_topic: "/odom"
    ros_type: "nav_msgs/Odometry"
    direction: "subscribe"
    rpc_service: "CoreX.rpc.RobotTelemetry"
    rpc_method: "GetOdometry"
    use_shm_topic: true      # 同时推入 SHM 供同机消费者
    rate_hz: 20              # RPC 轮询频率上限
```

#### 方向 B: CoreX → ROS (publish)

Cloud RPC 调用触发 ROS Topic 发布。

```
Cloud: RPC SetVelocity(linear_x=0.5, angular_z=0.2)
    ↓
TopicBridge::handlePublish()
    ├─→ [SHM] ShmTopicBus::publish()    同机消费者 < 10μs
    └─→ [TCP] ros::Publisher::publish() 跨机/标准 ROS 节点 ~50μs
    ↓
/cmd_vel → 机器人运动控制器
```

```yaml
topics:
  - ros_topic: "/cmd_vel"
    ros_type: "geometry_msgs/Twist"
    direction: "publish"
    rpc_service: "CoreX.rpc.MotionControl"
    rpc_method: "SetVelocity"
    use_shm_topic: true      # 同机消费者走 SHM 加速
```

### 6.5 Service 映射（通用 bytes 透传）

适用于调用 ROS Service（如 Gazebo 仿真控制）：

```yaml
services:
  - ros_service: "/gazebo/spawn_model"
    rpc_service: "CoreX.rpc.SimulationControl"
    rpc_method: "SpawnModel"
    timeout_ms: 5000
```

**原理**：Bridge 使用 `ros::SerializedMessage` 进行类型擦除，不依赖编译期 `.srv` 定义。RPC payload = 序列化的 ROS Service Request bytes，response = 序列化的 ROS Service Response bytes。

### 6.6 Action 映射（导航/机械臂控制）

ROS Action 是异步的 Goal→Feedback→Result 三段式模型，拆分为 4 个 RPC 方法：

```yaml
actions:
  - ros_action: "/move_base"
    ros_action_type: "move_base_msgs/MoveBaseAction"
    rpc_service: "CoreX.rpc.Navigation"
    rpc_method_start: "StartNavigation"       # 发起 Goal
    rpc_method_cancel: "CancelNavigation"      # 取消
    rpc_method_feedback: "GetNavigationFeedback" # 轮询进度
    rpc_method_result: "GetNavigationResult"     # 获取结果
    timeout_ms: 30000
```

**使用流程**：

```bash
# 1. 发起导航
goal_id=$(python3 rpc_client.py navigation StartNavigation \
    --target_x 5.0 --target_y 3.0 --target_yaw 0.0)

# 2. 轮询进度
while true; do
    python3 rpc_client.py navigation GetNavigationFeedback --goal_id "$goal_id"
    sleep 0.5
done

# 3. 获取结果
python3 rpc_client.py navigation GetNavigationResult --goal_id "$goal_id"
```

### 6.7 完整部署步骤（机器人侧）

```bash
# 步骤 1: 确保 ROS 环境
source /opt/ros/melodic/setup.bash
roscore &

# 步骤 2: 编译
cd /root/Cplus/CoreX
./build.sh
# CMake 自动检测 ROS → 编译 librrosbridge.a + CoreXDaemon (HAS_ROS_BRIDGE=1)

# 步骤 3: 编辑配置
vi apps/config/corex_daemon.yaml
# 设置 ros_bridge.enabled: true
# 根据需要配置 topics / services / actions

# 步骤 4: 启动
./build/CoreXDaemon --config apps/config/corex_daemon.yaml

# 步骤 5: 验证
# 新终端:
source /opt/ros/melodic/setup.bash
rostopic pub /cmd_vel geometry_msgs/Twist "linear: {x: 0.5}" -r 10 &
python3 apps/examples/python_client/rpc_client.py telemetry GetOdometry
```

---

## 七、ShmTopicBus 同机高性能通信

### 7.1 解决的问题

ROS 1 的同机 Topic 通信默认走 TCPROS（TCP/IP 协议栈），即使发布者和订阅者在同一台物理机上，也要经过：

```
序列化 → socket send buffer → kernel TCP stack → socket recv buffer → 反序列化
延迟: 50-200μs, 4 次内存拷贝
```

ShmTopicBus 用共享内存环形缓冲替代 TCPROS：

```
序列化 → CAS Push to RingBuffer → eventfd 通知 → CAS Pop → 反序列化
延迟: < 10μs, 2 次内存拷贝
```

### 7.2 使用方式

#### 方式 A：通过配置启用（TopicBridge 自动双通道）

```yaml
ros_bridge:
  topics:
    - ros_topic: "/cmd_vel"
      direction: "publish"
      use_shm_topic: true       # ★ 启用
```

TopicBridge 自动同时走 SHM 和 TCPROS。同机客户端从 SHM 读，跨机客户端从 TCPROS 收。

#### 方式 B：直接使用 ShmTopicBus API

```cpp
#include "src/ros_bridge/ShmTopicBus.hpp"

// ---- 发布者 ----
ShmTopicBus pub("/my_topic", 64*1024, 16, true);  // isCreator=true
pub.publish(data_ptr, data_size);

// ---- 订阅者 ----
ShmTopicBus sub("/my_topic", 64*1024, 16, false);  // isCreator=false
int efd = sub.subscribe();  // 获取 eventfd

// 将 efd 注册到 EventLoop:
EventLoop loop;
Channel channel(&loop, efd);
channel.setReadCallback([&]() {
    std::string msg;
    if (sub.tryRecv(msg)) {
        // 处理消息 — 从 SHM 直接读取, < 10μs
    }
});
channel.enableReading();
```

### 7.3 性能数据

| 消息大小 | TCPROS (localhost) | ShmTopicBus | 提升倍数 |
|:--------|:-----------------:|:-----------:|:------:|
| 256B (Twist) | ~50μs | ~5μs | **10x** |
| 4KB (LaserScan) | ~100μs | ~8μs | **12x** |
| 64KB (PointCloud) | ~300μs | ~15μs | **20x** |
| 6MB (1080p Image) | ~5ms | ~0.5ms | **10x** |

### 7.4 限制

- **仅限同机** — 跨物理机必须走 TCP
- **不替代 ROS Master** — Topic 命名/发现仍通过 ROS Master
- **最多 8 个订阅者** per topic
- **16 槽位环形缓冲** — 订阅者落后超过 16 帧会丢旧数据（传感器场景可接受）
- **创建者必须先于订阅者启动，后于订阅者销毁**

---

## 八、编写业务插件

### 8.1 插件规范

业务插件是一个 `.so` 文件，必须导出两个 C 函数：

```cpp
extern "C" RpcServiceAdapter* createService();
extern "C" void destroyService(RpcServiceAdapter* svc);
```

### 8.2 完整示例

参见 `apps/examples/math_plugin/math_service_plugin.cpp`：

```cpp
#include "../../src/rpc/RpcServiceAdapter.hpp"
#include "../../proto/math_service.pb.h"

class MathServiceImpl : public RpcServiceAdapter
{
public:
    MathServiceImpl()
        : RpcServiceAdapter(
              CoreX::rpc::MathRequest::descriptor()
                  ->file()->FindServiceByName("MathService"))
    {
        registerHandler("Add",
            [](Message* reqMsg, Message* respMsg) {
                auto* req  = static_cast<MathRequest*>(reqMsg);
                auto* resp = static_cast<MathResponse*>(respMsg);
                resp->set_result(req->a() + req->b());
                resp->set_success(true);
            });

        registerHandler("Sub",
            [](Message* reqMsg, Message* respMsg) {
                auto* req  = static_cast<MathRequest*>(reqMsg);
                auto* resp = static_cast<MathResponse*>(respMsg);
                resp->set_result(req->a() - req->b());
                resp->set_success(true);
            });
    }
};

extern "C" {
    RpcServiceAdapter* createService() {
        return new MathServiceImpl();
    }
    void destroyService(RpcServiceAdapter* svc) {
        delete svc;
    }
}
```

### 8.3 编译与部署

```bash
# 编译为 .so
g++ -shared -fPIC -std=c++17 \
    -I../src/rpc -I../proto -I../src/net -I../src/log -I../src/ipc \
    -o libmy_service.so my_service_plugin.cpp \
    $(pkg-config --cflags --libs protobuf)

# 部署
cp libmy_service.so /path/to/plugins/
# 重启 CoreXDaemon 或发送 SIGHUP (热加载，未来版本)
```

### 8.4 开发新服务的步骤

1. **定义 proto 文件** → `proto/your_service.proto`
2. **生成 C++ 代码** → `protoc --cpp_out=proto -Iproto proto/your_service.proto`
3. **编写插件** → 继承 `RpcServiceAdapter`，导出工厂函数
4. **编译为 .so** → 放入插件目录
5. **重启 CoreXDaemon** → 服务自动注册

---

## 九、客户端开发指南

### 9.1 Python 客户端

CoreX 提供零依赖 Python RPC 客户端（手写 Protobuf Wire Format 编解码）：

```bash
# 基本 RPC 调用
python3 apps/examples/python_client/rpc_client.py add --a 3 --b 5
# → Result: 8  Latency: 0.28 ms

# 指定服务端
python3 apps/examples/python_client/rpc_client.py \
    --host 192.168.1.100 --port 8080 add --a 100 --b 200

# 调用机器人遥测
python3 apps/examples/python_client/rpc_client.py \
    --host <robot_ip> --port 8080 telemetry GetOdometry

# 调用机器人控制
python3 apps/examples/python_client/rpc_client.py \
    --host <robot_ip> --port 8080 motion SetVelocity --linear_x 0.5
```

### 9.2 C++ 轻量客户端

使用 `RpcClient.hpp`（`apps/examples/robot_controller/RpcClient.hpp`）：

```cpp
#include "RpcClient.hpp"
#include "proto/robot_service.pb.h"

RpcClient client("192.168.1.100", 8080);
if (!client.connect()) { /* handle error */ }

// 同步 RPC 调用
CoreX::rpc::VelocityCommand req;
req.set_linear_x(0.5);
CoreX::rpc::ControlResponse resp;

if (client.call("MotionControl", "SetVelocity", req, resp)) {
    std::cout << "Success: " << resp.success() << std::endl;
}
```

### 9.3 任意语言客户端

CoreX RPC 使用标准的 **TLV 帧协议** + **Protobuf 序列化**：

```
┌──────────────┬──────────────┬─────────────────────────┐
│ Magic (4B)   │ Length (4B)  │ Protobuf Payload        │
│ 0x42414E41   │ Big-Endian   │ RpcMessage (序列化)     │
└──────────────┴──────────────┴─────────────────────────┘
```

实现步骤：
1. 用你语言的 Protobuf 库编译 `rpc_message.proto` 和业务 `.proto`
2. 构建 `RpcMessage{type=REQUEST, service="...", method="...", payload=<序列化请求>}`
3. 序列化 `RpcMessage`，加 8 字节 TLV 头
4. TCP socket 发送 → 接收响应 → 反序列化

### 9.4 同机 IPC 客户端（最低延迟）

```cpp
#include "src/ipc/IpcEndpoint.hpp"

// 连接到 CoreXDaemon 的 SHM IPC
IpcEndpoint ep("/corex_rpc_ipc", false);  // ATTACH 模式

// 发送 RPC 请求
std::string tlvPacket = buildTlvPacket(service, method, request);
ep.sendRequest(tlvPacket);
ep.notifyServer();

// 接收响应
std::string response;
ep.recvResponse(response, 5000);
```

---

## 十、生产环境部署

### 10.1 systemd 部署

```bash
# 1. 安装可执行文件
cd /root/Cplus/CoreX/build
cmake --install . --prefix /opt/corex

# 2. 创建目录
mkdir -p /etc/corex /opt/corex/plugins /var/log/corex

# 3. 配置文件
cp apps/config/corex_daemon.yaml /etc/corex/
# 编辑: 修改 plugins.directory、logging.path

# 4. 安装 systemd unit
cp apps/systemd/corex-daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable corex-daemon
sudo systemctl start corex-daemon

# 5. 管理
sudo systemctl status corex-daemon
journalctl -u corex-daemon -f
sudo systemctl restart corex-daemon
```

### 10.2 机器人侧部署（含 ROS Bridge）

```bash
# 1. 编译（需要 ROS 环境）
source /opt/ros/melodic/setup.bash
cd /root/Cplus/CoreX && ./build.sh

# 2. 安装
mkdir -p /opt/corex/bin /opt/corex/lib /opt/corex/plugins
cp build/CoreXDaemon /opt/corex/bin/
cp build/plugins/*.so /opt/corex/plugins/
cp apps/config/corex_daemon.yaml /etc/corex/

# 3. 编辑配置，启用 ROS Bridge
vi /etc/corex/corex_daemon.yaml
# ros_bridge.enabled: true
# 配置需要的 topics / services / actions

# 4. 创建 systemd unit（机器人启动时自动运行）
cat > /etc/systemd/system/corex-daemon.service << 'EOF'
[Unit]
Description=CoreX RPC Daemon with ROS Bridge
After=network.target
Wants=roscore.service

[Service]
Type=simple
Environment="ROS_MASTER_URI=http://localhost:11311"
ExecStartPre=/bin/bash -c 'source /opt/ros/melodic/setup.bash'
ExecStart=/opt/corex/bin/CoreXDaemon --config /etc/corex/corex_daemon.yaml
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable corex-daemon
```

### 10.3 云端部署（无 ROS）

```bash
# 云端服务器不需要 ROS——CoreXDaemon 作为纯 RPC 中间件运行
cd /root/Cplus/CoreX && ./build.sh
# → "ROS NOT found — skipping CoreX-ROS Bridge" (正常)
# → CoreXDaemon 仍正常编译，只是没有 Bridge 功能

cp build/CoreXDaemon /opt/corex/bin/
# 配置中 ros_bridge.enabled: false (默认)
```

### 10.4 安全加固

```bash
# 防火墙: 仅允许可信 IP 访问 RPC 端口
sudo iptables -A INPUT -p tcp --dport 8080 -s 192.168.1.0/24 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 8080 -j DROP

# 或使用 Nginx 反向代理 + TLS
# 参考: apps/USAGE.md §八 (Q: 支持 TLS/SSL 吗？)
```

---

## 十一、性能调优

### 11.1 工作线程数

```yaml
server:
  worker_threads: 4   # 子 Reactor 数
```

建议值：CPU 核心数的 50%-100%。IO 密集型可适当增加。

### 11.2 IPC Fast-Path

本地客户端（127.0.0.1）自动走 SHM IPC。确保 `ipc.enabled: true`。

### 11.3 开启延迟监控

```bash
# 编译时启用 timestamp 功能
./build.sh timestamp=1 test_rpc_benchmark

# 运行时查看延迟分解
./build/test_rpc_benchmark-log-timestamp --calls 10000
# 输出: P50/P99/P999 + 上行/处理/下行耗时分解
```

### 11.4 ShmTopicBus 调优

| 参数 | 默认 | 调优建议 |
|------|:---:|------|
| slot_count | 16 | 高频 Topic 增大到 32，低频减到 4 |
| max_msg_size | 64KB | 图像 Topic 设 10MB |
| subscriber_fds | 8 | 超过 8 个消费者时增大 `SHM_TOPIC_MAX_SUBSCRIBERS` |

### 11.5 火焰图分析

```bash
cd /root/Cplus/CoreX/tests
./perf_flamegraph_rpc.sh
# 生成 SVG 火焰图 → CPU 热点可视化
```

---

## 十二、故障排查

### 12.1 CoreXDaemon 无法启动

```bash
# 检查端口占用
ss -tlnp | grep 8080

# 检查配置文件语法
python3 -c "import yaml; yaml.safe_load(open('apps/config/corex_daemon.yaml'))"

# 检查插件目录
ls -la ./plugins/
# 确认 .so 文件存在且有执行权限
```

### 12.2 RPC 调用失败

```bash
# 检查服务是否注册
# 启动 banner 中会列出所有注册的服务

# 检查连接
telnet localhost 8080

# 使用 Python 客户端测试（会打印详细错误）
python3 apps/examples/python_client/rpc_client.py --host 127.0.0.1 add --a 1 --b 2
```

### 12.3 ROS Bridge 不工作

```bash
# 1. 检查 ROS Master
rostopic list
# 应能看到配置中的 topic

# 2. 检查编译
./build/CoreXDaemon --version
# 应显示是否包含 HAS_ROS_BRIDGE

# 3. 查看日志
tail -f log/corex_daemon.log | grep -i bridge

# 4. 检查配置
grep "enabled" apps/config/corex_daemon.yaml
# ros_bridge.enabled 应为 true

# 5. 手动验证 topic
rostopic echo /cmd_vel &
python3 apps/examples/python_client/rpc_client.py motion SetVelocity --linear_x 0.5
# /cmd_vel 应有输出
```

### 12.4 ShmTopicBus 通信异常

```bash
# 检查 SHM 段是否创建
ls -la /dev/shm/corex_topic_*

# 检查订阅者数量
# 在日志中搜索: grep "Subscribed to" log/corex_daemon.log

# 清理残留 SHM
rm -f /dev/shm/corex_topic_*
# 重启 CoreXDaemon
```

### 12.5 内存泄漏排查

```bash
# ASan 构建 + 压测
./build.sh asan test_echo_server
./build/test_echo_server_ASAN --mode server --port 8080 &
sleep 1
# 运行客户端压测...
# 退出后 ASan 报告泄漏
```

### 12.6 常见日志信息

| 日志 | 含义 | 处理 |
|------|------|------|
| `ROS Master not reachable` | roscore 未启动或网络不通 | 先启动 roscore |
| `Subscriber slots full` | 超过 8 个同 topic 订阅者 | 增大 `SHM_TOPIC_MAX_SUBSCRIBERS` |
| `Message too large` | 消息超过 maxMsgSize | 增大配置中的消息大小 |
| `Magic mismatch` | SHM 段损坏或版本不匹配 | 清理 /dev/shm/corex_topic_* |
| `Plugin loaded: 0 services` | 插件目录为空或 .so 加载失败 | 检查插件路径和编译 |

---

## 十三、API 参考

### 13.1 核心类

#### RpcServer

```cpp
class RpcServer {
public:
    RpcServer(EventLoop* loop, const std::string& ip,
              uint16_t port, const std::string& name);
    void start();
    void registerService(RpcServiceAdapter* adapter);
    void enableIpc(const std::string& shmName = "/corex_rpc_ipc");
    void setLatencyStats(RpcLatencyStats* stats);
};
```

#### RpcServiceAdapter

```cpp
class RpcServiceAdapter {
public:
    explicit RpcServiceAdapter(const ServiceDescriptor* svcDesc);
    virtual std::string dispatch(const std::string& method, const std::string& payload);
    virtual const std::string& serviceName() const;
    void registerHandler(const std::string& methodName, MethodHandler handler);
};
```

#### DynamicServiceAdapter (用于配置驱动适配器)

```cpp
class DynamicServiceAdapter : public RpcServiceAdapter {
public:
    explicit DynamicServiceAdapter(const std::string& svcName);
    std::string dispatch(const std::string& method, const std::string& payload) override;
    void registerMethod(const std::string& methodName, DynamicHandler handler);
};
```

#### ShmTopicBus

```cpp
class ShmTopicBus {
public:
    ShmTopicBus(const std::string& topicName, size_t maxMsgSize,
                int slotCount = 16, bool isCreator = false);
    bool publish(const void* data, size_t size);
    int subscribe();                              // 返回 eventfd
    void unsubscribe(int fd);
    bool tryRecv(std::string& outMsg);            // 非阻塞
    bool recvWait(std::string& outMsg, int timeoutMs = 1000);  // 阻塞等待
    int subscriberCount() const;
    uint64_t lastWriteSeq() const;
};
```

### 13.2 Wire Protocol

```
TLV 帧格式:
[4B Magic "BANA" (0x42414E41, big-endian)]
[4B Payload Length (big-endian)]
[NB Protobuf RpcMessage]

RpcMessage 结构 (rpc_message.proto):
  type:      REQUEST=0 / RESPONSE=1 / ERROR=2
  id:        关联 ID (uint64)
  service:   "CoreX.rpc.MathService"
  method:    "Add"
  payload:   序列化的请求/响应 bytes
  error:     错误码 (仅 type=ERROR)
  client_send_ts / server_recv_ts / server_send_ts:  时间戳 (us)
```

### 13.3 Proto 消息速查

| Proto 文件 | 包含的 Message / Service |
|-----------|------------------------|
| `rpc_message.proto` | `RpcMessage` (REQUEST/RESPONSE/ERROR 信封) |
| `math_service.proto` | `MathService.Add/Sub`, `MathRequest`, `MathResponse` |
| `robot_service.proto` | `RobotTelemetry` (GetOdometry/GetStatus/GetImage/...), `MotionControl` (SetVelocity/Stop), `SimulationControl`, `Navigation` |
| `ros_messages.proto` | `JointState`, `Image`, `LaserScan`, `PointCloud2`, `TFMessage`, `Imu`, `BatteryState` 等 |

---

## 附录 A: 快速参考卡片

```bash
# ═══════════ 编译 ═══════════
./build.sh                            # 全量 Release 构建
./build.sh debug                      # Debug 构建
./build.sh <target>                   # 单目标: test_ros_bridge, test_rpc_benchmark
./build.sh clean                      # 清理

# ═══════════ 启动 ═══════════
./build/CoreXDaemon --config apps/config/corex_daemon.yaml
./build/CoreXDaemon --daemon          # 后台模式

# ═══════════ 测试 ═══════════
./build/test_ros_bridge               # 11 项单元测试
./build/test_rpc_benchmark --calls 10000 --threads 2
./build/test_echo_server --mode server --port 8080
./build/test_echo_server --mode client --connections 1000

# ═══════════ RPC 客户端 ═══════════
python3 apps/examples/python_client/rpc_client.py add --a 3 --b 5
python3 apps/examples/python_client/rpc_client.py telemetry GetOdometry
./build/host_controller --host 127.0.0.1 --port 8080

# ═══════════ 运维 ═══════════
kill -TERM $(pidof CoreXDaemon)       # 优雅关闭
tail -f log/corex_daemon.log          # 查看日志
ls /dev/shm/corex_*                   # 查看 SHM 段
```

## 附录 B: 相关文档索引

| 文档 | 路径 | 内容 |
|------|------|------|
| 项目 README | [README.md](README.md) | 项目概览、快速开始 |
| 本手册 | [DEPLOYMENT.md](DEPLOYMENT.md) | 完整部署与使用 |
| 使用文档 | [apps/USAGE.md](apps/USAGE.md) | 守护进程 + 嵌入式 + ROS Bridge |
| AI 开发指南 | [CLAUDE.md](CLAUDE.md) | 架构详解、已知问题 |
| Bridge 模块 | [src/ros_bridge/README.md](src/ros_bridge/README.md) | Bridge API 与配置 |
| 插件示例 | [apps/examples/math_plugin/README.md](apps/examples/math_plugin/README.md) | 业务插件开发 |
| 旧插件迁移 | [apps/plugins/ros/DEPRECATED.md](apps/plugins/ros/DEPRECATED.md) | ROS 插件迁移指南 |
| RPC 压测 | [tests/test_rpc_benchmark.md](tests/test_rpc_benchmark.md) | 压测工具用法 |
