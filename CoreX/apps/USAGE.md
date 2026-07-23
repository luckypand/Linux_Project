# CoreX 使用文档

> 适用于 CoreX v1.0.0 | 最后更新：2026-06-26

---

## 一、apps/ 目录结构概览

```
apps/
├── USAGE.md                            # ★ 本文件 — 完整使用文档
├── BUG_ANALYSIS.md                     # 已知 Bug 分析与修复指南
│
├── daemon/                             # 守护进程源代码
│   ├── CoreXDaemon.hpp/.cpp            # 生命周期：init → loadPlugins → start → wait → shutdown
│   ├── ConfigManager.hpp/.cpp          # 基于 yaml-cpp 的配置文件解析
│   ├── PluginLoader.hpp/.cpp           # dlopen/dlsym 动态加载 .so 业务插件
│   ├── SignalHandler.hpp/.cpp          # SIGTERM/SIGINT 信号处理 + 优雅关闭
│   └── main.cpp                        # 入口：参数解析 → fork 后台 → 信号注册 → 事件循环
│
├── config/                             # 配置文件
│   └── corex_daemon.yaml               # 默认配置模板（端口、线程数、IPC、插件路径、日志、★ ROS Bridge）
│
├── examples/                           # 示例
│   ├── math_plugin/                    # 业务插件示例（Add/Sub 计算服务）
│   │   ├── math_service_plugin.cpp     #   继承 RpcServiceAdapter 实现业务逻辑
│   │   ├── CMakeLists.txt              #   独立编译脚本
│   │   └── README.md                   #   插件编译/部署说明
│   ├── robot_controller/               # ★ 机器人遥控面板示例
│   │   ├── main.cpp                    #   交互式键盘控制 + 遥测轮询 (20Hz)
│   │   └── RpcClient.hpp               #   轻量级 C++ RPC 客户端（纯 TCP + Protobuf）
│   └── python_client/                  # Python RPC 客户端示例
│       └── rpc_client.py               #   零依赖，手写 Protobuf Wire Format 编解码
│
├── plugins/ros/                        # ★ ROS 桥接插件（已被 src/ros_bridge 取代）
│   ├── control_plugin.cpp              #   CoreX→ROS /cmd_vel 运动控制（旧版）
│   ├── telemetry_plugin.cpp            #   ROS→CoreX /odom 遥测（旧版）
│   └── DEPRECATED.md                   #   弃用说明 + 迁移指南
│
└── systemd/                            # 生产部署
    └── corex-daemon.service            # systemd unit 模板
```

### 各目录职责

| 目录 | 作用 | 面向用户 |
|------|------|---------|
| `daemon/` | CoreXDaemon 可执行文件的源码。封装了配置解析、插件加载、信号处理、生命周期管理等通用逻辑，**与具体业务解耦**。 | 运维 / 架构师 |
| `config/` | 默认 YAML 配置模板。部署时复制到 `/etc/corex/` 并按需修改。 | 运维 |
| `examples/math_plugin/` | 演示如何编写业务插件 `.so`。继承 `RpcServiceAdapter`，导出 `createService`/`destroyService`。 | 业务开发者 |
| `examples/robot_controller/` | ★ 演示如何使用 C++ RPC 客户端遥控机器人。包含交互式键盘控制和遥测面板。 | 机器人开发者 |
| `examples/python_client/` | 演示任意语言如何通过 TCP + Protobuf 调用 CoreX RPC。Python 版零外部依赖。 | 客户端开发者 |
| `plugins/ros/` | ★ ROS 桥接插件（旧版，已被 `src/ros_bridge/` 取代）。保留作为教学参考。 | 机器人开发者 |
| `systemd/` | systemd unit 文件模板，用于生产环境的自动启动、重启、日志收集。 | 运维 |

---

## 二、守护进程模式（CoreXDaemon）

### 2.1 架构概览

```
┌──────────────────────────────────────────────────────────┐
│                    CoreXDaemon 进程                       │
│                                                          │
│  main.cpp → CoreXDaemon → ConfigManager (corex_daemon.yaml)│
│                │              PluginLoader (dlopen .so)   │
│                ▼                                         │
│         RpcServer ─────────────────────┐                  │
│         (TcpServer + Codec)            │                  │
│           │                            │                  │
│     ┌─────┴─────┐              ┌──────┴──────┐           │
│     │ TCP :8080  │              │ IPC (SHM)   │           │
│     │ (多语言)    │              │ (本地极速)   │           │
│     └───────────┘              └─────────────┘           │
│                                                          │
│  ◀───── 业务插件 (.so) ─────▶                            │
│  MathService  UserService  OrderService ...              │
└──────────────────────────────────────────────────────────┘
```

- 配置文件驱动，零代码启动
- 插件 `.so` 放入目录即自动注册
- TCP + IPC 双通道，自动检测本地客户端走共享内存加速
- 优雅关闭：SIGTERM → 停止 accept → 等待请求完成 → 清理 SHM → 卸载插件

### 2.2 构建

```bash
cd CoreX

# 全量构建
./build.sh

# 构建产物
ls build/CoreXDaemon              # 守护进程可执行文件
ls build/plugins/libmath_service.so  # 示例业务插件
```

**新增依赖**（相对于 core 库）：

```bash
sudo apt install -y libyaml-cpp-dev   # YAML 配置解析
```

### 2.3 配置文件

守护进程通过 YAML 配置文件驱动，默认路径 `corex_daemon.yaml`。

```yaml
server:
  name: "CoreXDaemon"       # 服务器名称
  host: "0.0.0.0"           # 监听地址
  port: 8080                # TCP 端口
  worker_threads: 4         # IO 工作线程数

ipc:
  enabled: true                       # 启用共享内存 IPC 加速
  shm_name: "/corex_rpc_ipc"          # 共享内存段名称

plugins:
  directory: "./plugins"              # .so 插件搜索目录
  autoload: true                      # 启动时自动加载

logging:
  level: "INFO"                       # DEBUG | INFO | WARN | ERROR | FATAL
  path: "./log"                       # 日志输出目录

monitoring:
  health_check_port: 9090             # 健康检查端口（预留）
  latency_stats: false                # 延迟统计（预留）
```

### 2.4 启动

```bash
# 前台运行（查看日志和 banner）
./build/CoreXDaemon --config apps/config/corex_daemon.yaml

# 后台运行（守护进程化）
./build/CoreXDaemon --config apps/config/corex_daemon.yaml --daemon

# 自定义端口
./build/CoreXDaemon --config apps/config/corex_daemon.yaml --port 9090

# 查看版本
./build/CoreXDaemon --version
```

启动成功后输出：

```
╔══════════════════════════════════════════════╗
║        CoreX RPC Daemon v1.0.0               ║
╠══════════════════════════════════════════════╣
║  TCP  : 0.0.0.0:8080                        ║
║  IPC  : /corex_rpc_ipc                      ║
║  Services: 1 loaded                         ║
║    - CoreX.rpc.MathService                  ║
╚══════════════════════════════════════════════╝
```

### 2.5 关闭

```bash
# 优雅关闭（推荐）
kill -TERM $(pidof CoreXDaemon)

# 强制关闭
kill -KILL $(pidof CoreXDaemon)
```

优雅关闭流程：信号 → 停止 accept → 等待进行中请求（30s 超时）→ 排空 IPC 环 → 清理 SHM → 卸载插件 → 退出

---

## 三、业务插件开发（编写你自己的 RPC 服务）

### 3.1 插件规范

每个业务插件是一个 `.so` 文件，必须导出两个 C 函数：

```cpp
extern "C" RpcServiceAdapter* createService();
extern "C" void destroyService(RpcServiceAdapter* svc);
```

### 3.2 示例：MathService 插件

完整源码见 [examples/math_plugin/math_service_plugin.cpp](examples/math_plugin/math_service_plugin.cpp)。

```cpp
// 1. 继承 RpcServiceAdapter
class MathServiceImpl : public RpcServiceAdapter
{
public:
    MathServiceImpl()
        : RpcServiceAdapter(
              // 从 proto 生成的描述符中查找 ServiceDescriptor
              CoreX::rpc::MathRequest::descriptor()
                  ->file()->FindServiceByName("MathService")
          )
    {
        // 2. 注册 RPC 方法处理函数
        registerHandler("Add",
            [](Message* reqMsg, Message* respMsg) {
                auto* req  = static_cast<MathRequest*>(reqMsg);
                auto* resp = static_cast<MathResponse*>(respMsg);
                resp->set_result(req->a() + req->b());
                resp->set_success(true);
            }
        );

        registerHandler("Sub",
            [](Message* reqMsg, Message* respMsg) {
                auto* req  = static_cast<MathRequest*>(reqMsg);
                auto* resp = static_cast<MathResponse*>(respMsg);
                resp->set_result(req->a() - req->b());
                resp->set_success(true);
            }
        );
    }
};

// 3. 导出工厂函数
extern "C" {
    RpcServiceAdapter* createService() {
        return new MathServiceImpl();
    }
    void destroyService(RpcServiceAdapter* svc) {
        delete svc;
    }
}
```

### 3.3 编译插件

```bash
# 方式 1: 使用项目 CMake（推荐）
cd CoreX && ./build.sh
# 产物在 build/plugins/libmath_service.so

# 方式 2: 手动编译
g++ -shared -fPIC -o libmy_service.so my_service_plugin.cpp \
    -I../src/rpc -I../src/net -I../src/log -I../src/ipc -I../proto \
    $(pkg-config --cflags --libs protobuf)
```

### 3.4 部署插件

将 `.so` 放入配置文件中指定的 `plugins.directory`：

```bash
cp libmy_service.so /path/to/plugins/
# 重启 CoreXDaemon 或等待热加载（未来版本）
```

### 3.5 开发新服务的步骤

1. **定义 proto 文件** — 创建 `your_service.proto`
2. **生成 C++ 代码** — `protoc --cpp_out=proto -Iproto proto/your_service.proto`
3. **编写插件** — 继承 `RpcServiceAdapter`，导出 `createService`/`destroyService`
4. **编译为 .so** — 将 `.so` 放入插件目录
5. **启动/重启** CoreXDaemon — 服务自动注册

---

## 四、客户端调用

### 4.1 Python 客户端

零外部依赖，手写 Protobuf Wire Format 编解码。

```bash
# 基本用法
python3 apps/examples/python_client/rpc_client.py add --a 3 --b 5
# → Result: 8  Latency: 0.28 ms

python3 apps/examples/python_client/rpc_client.py sub --a 10 --b 3
# → Result: 7  Latency: 0.27 ms

# 指定服务器
python3 apps/examples/python_client/rpc_client.py \
    --host 192.168.1.100 --port 8080 add --a 100 --b 200

# 查看帮助
python3 apps/examples/python_client/rpc_client.py --help
```

### 4.2 其他语言客户端

CoreX RPC 使用标准的 **TLV 帧协议** + **Protobuf 序列化**，任何语言都能实现客户端：

```
┌──────────────┬──────────────┬─────────────────────────┐
│ Magic (4B)   │ Length (4B)  │ Protobuf Payload        │
│ 0x42414E41   │ Big-Endian   │ RpcMessage (序列化)      │
└──────────────┴──────────────┴─────────────────────────┘
```

客户端实现要点：
1. 用你语言的 Protobuf 库编译项目中的 `proto/rpc_message.proto` 和 `proto/math_service.proto`
2. 构建 `RpcMessage` 信封（type=REQUEST, service="CoreX.rpc.MathService", method="Add", payload=<序列化的请求>）
3. 序列化 `RpcMessage`，前面加上 8 字节 TLV 头（大端序）
4. 通过 TCP socket 发送
5. 接收 8 字节头 + payload，反序列化得到响应

参考实现：[examples/python_client/rpc_client.py](examples/python_client/rpc_client.py)

### 4.3 C++ 客户端（嵌入式库模式）

如果宿主程序已经是 C++17，可以直接链接 `librpc.a`：

```cpp
#include "src/rpc/RpcServer.hpp"
#include "proto/math_service.pb.h"

EventLoop loop;
MathServiceImpl mathService;
RpcServer server(&loop, "0.0.0.0", 8080, "MyServer");
server.registerService(&mathService);
server.start();
loop.loop();  // 阻塞
```

---

## 五、生产部署（systemd）

### 5.1 安装

```bash
# 安装可执行文件和库
cd CoreX/build
cmake --install . --prefix /opt/corex

# 安装配置文件
mkdir -p /etc/corex
cp apps/config/corex_daemon.yaml /etc/corex/

# 安装 systemd unit
cp apps/systemd/corex-daemon.service /etc/systemd/system/

# 创建插件目录
mkdir -p /opt/corex/plugins
cp build/plugins/libmath_service.so /opt/corex/plugins/

# 修改配置中的路径
# /etc/corex/corex_daemon.yaml:
#   plugins.directory: "/opt/corex/plugins"
#   logging.path: "/var/log/corex"
```

### 5.2 启动和管理

```bash
sudo systemctl daemon-reload
sudo systemctl enable corex-daemon
sudo systemctl start corex-daemon
sudo systemctl status corex-daemon

# 查看日志
journalctl -u corex-daemon -f

# 重启
sudo systemctl restart corex-daemon
```

---

## 六、嵌入式库模式

如果你不需要独立进程，可以直接在工程中链接 CoreX 静态库。

### 6.1 CMake 集成

```cmake
# 方式 1: add_subdirectory
add_subdirectory(path/to/CoreX)
target_link_libraries(your_app rpc)

# 方式 2: find_package (安装后)
find_package(CoreX REQUIRED)
target_link_libraries(your_app CoreX::rpc)
```

### 6.2 手动链接

```bash
g++ -std=c++17 your_app.cpp \
    -Ipath/to/CoreX/src/net -Ipath/to/CoreX/src/rpc \
    -Ipath/to/CoreX/src/ipc -Ipath/to/CoreX/src/log \
    -Ipath/to/CoreX/proto \
    -Lpath/to/CoreX/build -lrpc \
    -lprotobuf -lpthread -lrt
```

### 6.3 代码示例

```cpp
#include "src/net/EventLoop.hpp"
#include "src/rpc/RpcServer.hpp"
#include "src/rpc/RpcServiceAdapter.hpp"

// 1. 定义你的服务（继承 RpcServiceAdapter）
class MyService : public RpcServiceAdapter { /* ... */ };

// 2. 启动服务
int main() {
    EventLoop loop;
    MyService mySvc;
    RpcServer server(&loop, "0.0.0.0", 8080, "MyApp");
    server.registerService(&mySvc);
    server.start();
    loop.loop();  // 阻塞
}
```

---

## 七、命令行参考

```
CoreXDaemon [选项]

选项:
  -c, --config <path>   配置文件路径（默认: corex_daemon.yaml）
  -d, --daemon          以守护进程模式运行（fork 到后台）
  -p, --port <port>     覆盖配置文件中的端口号
  --host <host>         覆盖配置文件中的监听地址
  -v, --version         打印版本信息
  -h, --help            打印帮助信息
```

---

## 八、常见问题

### Q: 插件加载失败，提示 "undefined symbol"？

确保 `CoreXDaemon` 编译时使用了 `--whole-archive`（已在 CMakeLists.txt 中配置）。Protobuf 的全局描述符表要求 `.pb.cc` 在可执行文件中完整链接。

### Q: 如何在不重启的情况下加载新插件？

当前版本需要在编译期链接 Protobuf 描述符（`librpc.a` 已包含）。未来版本计划支持独立 `.so` 热加载。当前可通过 SIGHUP 信号触发重新扫描插件目录（待实现）。

### Q: IPC 快速通道需要什么条件？

服务端和客户端必须在同一台物理机上，使用相同的共享内存名称（默认 `/corex_rpc_ipc`）。当前 Python 客户端示例通过 TCP 通信，IPC 通道需要 C++ 客户端支持。

### Q: 如何添加更多工作线程？

修改 `corex_daemon.yaml` 中的 `server.worker_threads`。注意这是子 Reactor 线程数，主 Reactor 额外占用一个线程。

### Q: 支持 TLS/SSL 吗？

当前版本不支持。可以通过在 CoreXDaemon 前面放置 Nginx/HAProxy 作为 TLS 终端来实现加密传输。

---

---

## 十、ROS Bridge 集成（CoreX ↔ ROS 协议网关）

> **适用场景：机器人/自动驾驶** — 将 ROS Topic/Service/Action 抽象为 CoreX RPC 服务，实现云端↔机器人双向通信。
>
> **前置条件**：需要 ROS 环境（roscore + roscpp），详见 [src/ros_bridge/README.md](../src/ros_bridge/README.md)。

### 10.1 架构概览

```
┌──────────────────────────────────────────────────────────┐
│                    CoreXDaemon 进程 (机器人侧)              │
│                                                          │
│  云服务器 ←─TCP─→ RpcServer ←─registerService─┐           │
│                                                  │        │
│  ┌─────────────────────────────────────────┐     │        │
│  │          RosBridgeEngine                 │     │        │
│  │                                          │     │        │
│  │  BridgeConfig (YAML) ──→ ┌──────────────┤     │        │
│  │                          │ TopicBridge   │     │        │
│  │  topics:                 │  - /cmd_vel   │─────┼──→ RPC │
│  │    - /cmd_vel (publish)  │  - /odom      │     │  注册   │
│  │    - /odom (subscribe)   │  - /scan      │     │        │
│  │                          └──────────────┘     │        │
│  │                          ┌──────────────┐     │        │
│  │  services:               │ ServiceBridge │     │        │
│  │    - /gazebo/spawn_model │  - bytes 透传  │     │        │
│  │                          └──────────────┘     │        │
│  │                          ┌──────────────┐     │        │
│  │  actions:                │ ActionBridge  │     │        │
│  │    - /move_base          │  - Start/Cancel│    │        │
│  │                          └──────────────┘     │        │
│  │                          ┌──────────────┐     │        │
│  │  ★ robot_id: "robot_001" │ robot_id过滤   │     │        │
│  │                          └──────────────┘     │        │
│  └──────────────┬──────────────────────────┘     │        │
│                 │ ROS API                         │        │
│  ┌──────────────▼──────────────────────────┐     │        │
│  │          RosNodeManager                  │     │        │
│  │  ros::NodeHandle + AsyncSpinner          │     │        │
│  └──────────────┬──────────────────────────┘     │        │
└─────────────────┼─────────────────────────────────┘
                  │
     ┌────────────┼────────────┐
     ▼            ▼            ▼
  ROS Topic   ROS Service  ROS Action
  /cmd_vel    /gazebo/...  /move_base
  /odom
```

**数据流向**：

| 方向 | 流程 | 典型场景 |
|------|------|---------|
| CoreX → ROS (publish) | 云端 RPC → TopicBridge → `ros::Publisher` → ROS Topic | 云端控制机器人运动 (`/cmd_vel`) |
| ROS → CoreX (subscribe) | ROS Topic → `ros::Subscriber` 回调 → 缓存 → RPC 查询 | 机器人状态回传 (`/odom`, `/joint_states`) |
| ROS Service | 云端 RPC → ServiceBridge → `ros::service::call()` → 响应 | Gazebo 仿真控制 |
| ROS Action | 云端 RPC → ActionBridge → Goal/Cancel/Feedback/Result | 导航任务 (`/move_base`) |

### 10.2 配置参考

在 `corex_daemon.yaml` 中添加 `ros_bridge` 配置段：

```yaml
ros_bridge:
  # ---- 基础设置 ----
  enabled: true                        # 启用 ROS Bridge（默认 false）
  node_name: "corex_ros_bridge"        # ROS 节点名
  spinner_threads: 2                   # AsyncSpinner 线程数

  # ---- Topic 双向映射 ----
  topics:
    # 方向 subscribe: ROS → CoreX（Bridge 订阅 ROS，供云端查询）
    - ros_topic: "/odom"
      ros_type: "nav_msgs/Odometry"
      direction: "subscribe"
      rpc_service: "CoreX.rpc.RobotTelemetry"
      rpc_method: "GetOdometry"
      rate_hz: 20                      # 缓存更新频率上限 (Hz)
      queue_size: 10

    - ros_topic: "/joint_states"
      ros_type: "sensor_msgs/JointState"
      direction: "subscribe"
      rpc_service: "CoreX.rpc.RobotTelemetry"
      rpc_method: "GetJointStates"
      rate_hz: 50
      queue_size: 10

    - ros_topic: "/camera/image_raw"
      ros_type: "sensor_msgs/Image"
      direction: "subscribe"
      rpc_service: "CoreX.rpc.RobotTelemetry"
      rpc_method: "GetImage"
      use_shm: true                    # ★ 高频大消息走共享内存
      rate_hz: 30
      queue_size: 5

    - ros_topic: "/scan"
      ros_type: "sensor_msgs/LaserScan"
      direction: "subscribe"
      rpc_service: "CoreX.rpc.RobotTelemetry"
      rpc_method: "GetLaserScan"
      rate_hz: 10
      queue_size: 10

    # 方向 publish: CoreX → ROS（云端 RPC 调用 → 发布到 ROS）
    - ros_topic: "/cmd_vel"
      ros_type: "geometry_msgs/Twist"
      direction: "publish"
      rpc_service: "CoreX.rpc.MotionControl"
      rpc_method: "SetVelocity"
      queue_size: 10

  # ---- ROS Service 映射（使用通用 bytes 透传）----
  services:
    - ros_service: "/gazebo/spawn_model"
      rpc_service: "CoreX.rpc.SimulationControl"
      rpc_method: "SpawnModel"
      timeout_ms: 5000

    - ros_service: "/controller_manager/switch_controller"
      rpc_service: "CoreX.rpc.ControllerManager"
      rpc_method: "SwitchController"
      timeout_ms: 3000

  # ---- ROS Action 映射（拆分为 4 个 RPC 方法）----
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

### 10.3 ★ Robot ID 多机器人定向控制

在多机器人场景下，每台机器人通过 `ros_bridge.robot_id` 配置唯一标识。云端在 RPC 请求中携带 `robot_id` 即可定向控制指定机器人。

**配置方式**（每台机器人配置不同的 `robot_id`）：

```yaml
ros_bridge:
  enabled: true
  robot_id: "robot_001"    # ★ 修改此处为每台机器人分配唯一 ID
```

**协议层**：`RpcMessage.robot_id` (field 10) — 空串表示"任意机器人"（广播/兼容模式）。

**过滤规则**（[RpcServer.cpp:96-112](../src/rpc/RpcServer.cpp#L96-L112)）：
- 本机 `robot_id` 为空 → 不校验，所有请求放行（向后兼容）
- 请求 `robot_id` 为空 → 不校验，放行（兼容旧客户端）
- 两者均非空且不匹配 → 返回 `INVALID_REQUEST` 错误

**Python 客户端**：
```bash
# 定向控制 robot_002
python3 rpc_client.py --host <ip> --robot_id robot_002 motion SetVelocity --linear_x 0.5

# 不指定 robot_id = 任意机器人（兼容模式）
python3 rpc_client.py --host <ip> motion SetVelocity --linear_x 0.5
```

**⚠️ C++ 客户端** ([RpcClient.hpp](examples/robot_controller/RpcClient.hpp)) 当前未实现 `robot_id` 设置，需要时在 `call()` 中添加 `rpcMsg.set_robot_id(targetRobotId)`。

### 10.4 快速开始

```bash
# 1. 启动 ROS Master
roscore &

# 2. 编译（自动检测 ROS 环境，编译 librrosbridge.a）
cd CoreX && ./build.sh

# 3. 编辑配置文件：设置 enabled: true
vi apps/config/corex_daemon.yaml

# 4. 启动 CoreXDaemon（自动初始化 ROS Bridge）
./build/CoreXDaemon --config apps/config/corex_daemon.yaml

# 启动 banner 示例：
# ╔══════════════════════════════════════════════╗
# ║        CoreX RPC Daemon v1.0.0               ║
# ╠══════════════════════════════════════════════╣
# ║  TCP  : 0.0.0.0:8080                        ║
# ║  IPC  : /corex_rpc_ipc                      ║
# ║  Services: 4 loaded                         ║
# ║    - CoreX.rpc.RobotTelemetry               ║
# ║    - CoreX.rpc.MotionControl                ║
# ║    - CoreX.rpc.SimulationControl            ║
# ║    - CoreX.rpc.Navigation                   ║
# ║  ROS Bridge: /cmd_vel→publish, /odom→subscribe ║
# ╚══════════════════════════════════════════════╝

# 5. 机器人遥控（交互式）
./build/host_controller --host 127.0.0.1 --port 8080
# 命令: f 0.5 (前进)  b 0.3 (后退)  l 0.5 (左转)  r 0.5 (右转)  s (急停)  q (退出)

# 6. Python 客户端查询遥测
python3 apps/examples/python_client/rpc_client.py telemetry GetOdometry
python3 apps/examples/python_client/rpc_client.py telemetry GetStatus
```

### 10.5 典型使用场景

#### 场景 A：云端远程控制机器人移动

```bash
# 云端服务器 —— 通过 TCP 连接机器人 CoreXDaemon
python3 apps/examples/python_client/rpc_client.py \
    --host <robot_ip> --port 8080 \
    motion SetVelocity --linear_x 0.5 --angular_z 0.2

# 急停
python3 apps/examples/python_client/rpc_client.py \
    --host <robot_ip> --port 8080 \
    motion Stop
```

**内部流程**：
```
云端 RPC SetVelocity(linear_x=0.5, angular_z=0.2)
  ↓ TCP TLV + Protobuf
机器人 CoreXDaemon
  ↓ RpcServer::dispatchTable["CoreX.rpc.MotionControl"]
  ↓ DynamicServiceAdapter::dispatch("SetVelocity", payload)
  ↓ TopicBridge::handlePublish(payload)
  ↓ ros::Publisher::publish(geometry_msgs::Twist)
  ↓ /cmd_vel
机器人底盘控制器 → 电机转动
```

#### 场景 B：云端监控机器人状态

```bash
# 轮询里程计（20Hz 刷新）
watch -n 0.05 'python3 apps/examples/python_client/rpc_client.py \
    --host <robot_ip> --port 8080 telemetry GetOdometry'

# 获取电池状态
python3 apps/examples/python_client/rpc_client.py \
    --host <robot_ip> --port 8080 telemetry GetStatus
```

**内部流程**：
```
机器人 ROS /odom 发布 (100Hz)
  ↓ ros::Subscriber 回调
  ↓ TopicBridge::onRosMessage() → 更新缓存
云端 RPC GetOdometry()
  ↓ RpcServer → DynamicServiceAdapter::dispatch
  ↓ TopicBridge::handleGetCached() → 返回最新缓存
  ↓ TCP TLV + Protobuf
云端收到 OdometryResponse {pos_x, pos_y, vel_x, ...}
```

#### 场景 C：Gazebo 仿真控制

```yaml
# 配置
services:
  - ros_service: "/gazebo/spawn_model"
    rpc_service: "CoreX.rpc.SimulationControl"
    rpc_method: "SpawnModel"
```

```python
# 通过 RPC 调用 Gazebo 的 ROS Service
python3 apps/examples/python_client/rpc_client.py \
    simulation SpawnModel \
    --model_name "my_robot" --pos_x 1.0 --pos_y 2.0
```

#### 场景 D：自主导航

```yaml
# 配置
actions:
  - ros_action: "/move_base"
    ros_action_type: "move_base_msgs/MoveBaseAction"
    rpc_service: "CoreX.rpc.Navigation"
    rpc_method_start: "StartNavigation"
    rpc_method_cancel: "CancelNavigation"
    rpc_method_feedback: "GetNavigationFeedback"
    rpc_method_result: "GetNavigationResult"
```

```bash
# 1. 发起导航任务
goal_id=$(python3 apps/examples/python_client/rpc_client.py \
    navigation StartNavigation \
    --target_x 5.0 --target_y 3.0 --target_yaw 0.0)

# 2. 轮询进度
while true; do
    python3 apps/examples/python_client/rpc_client.py \
        navigation GetNavigationFeedback --goal_id "$goal_id"
    sleep 0.5
done

# 3. 获取最终结果
python3 apps/examples/python_client/rpc_client.py \
    navigation GetNavigationResult --goal_id "$goal_id"
```

### 10.6 共享内存加速 (SHM)

CoreX 提供两种 SHM 加速模式：

#### 模式 A：SHM Topic 直通 (`use_shm_topic: true`) ★ 推荐

**用共享内存替代同机 ROS TCPROS**。发布者写入共享内存环形缓冲，订阅者通过 eventfd 唤醒读取。同机延迟从 TCPROS 的 50-200μs 降至 **< 10μs**。

```yaml
topics:
  - ros_topic: "/cmd_vel"
    ros_type: "geometry_msgs/Twist"
    direction: "publish"
    rpc_service: "CoreX.rpc.MotionControl"
    rpc_method: "SetVelocity"
    use_shm_topic: true          # ★ 启用 SHM 直通

  - ros_topic: "/odom"
    ros_type: "nav_msgs/Odometry"
    direction: "subscribe"
    rpc_service: "CoreX.rpc.RobotTelemetry"
    rpc_method: "GetOdometry"
    use_shm_topic: true
```

**工作原理**：TopicBridge 在 publish/subscribe 时同时写入 SHM 和 ROS TCPROS。同机 CoreX 客户端直接从 SHM 读取（< 10μs），跨机/标准 ROS 节点仍通过 TCPROS 接收。

**客户端代码示例**（C++ 同机高性能订阅者）：

```cpp
#include "src/ros_bridge/ShmTopicBus.hpp"

// 附加到已有 topic 的 SHM 段
ShmTopicBus sub("/cmd_vel", 64*1024, 16, false);  // isCreator=false
int efd = sub.subscribe();  // 获取 eventfd

// 注册到 EventLoop/epoll
// ...

// 读取最新消息
std::string msg;
if (sub.tryRecv(msg)) {
    // msg 是 ROS 序列化后的原始数据，可直接反序列化
}
```

#### 模式 B：大消息三缓冲 (`use_shm: true`)

对于图像、点云等大数据（单帧 > 100KB），启用三缓冲共享内存：

```yaml
topics:
  - ros_topic: "/camera/image_raw"
    ros_type: "sensor_msgs/Image"
    direction: "subscribe"
    rpc_service: "CoreX.rpc.RobotTelemetry"
    rpc_method: "GetImage"
    use_shm: true              # 三缓冲共享内存
```

**性能对比**：

| 方式 | 同机延迟 | 适用场景 |
|------|:------:|------|
| TCPROS（默认） | 50-200μs | 跨机 / 标准 ROS 节点 |
| **SHM Topic 直通** (`use_shm_topic`) | **< 10μs** | 同机 CoreX 节点高频 Topic |
| SHM 三缓冲 (`use_shm`) | < 1ms | 大消息（图像/点云）RPC 查询 |
| 两者同时启用 | < 10μs (Topic) + < 1ms (RPC) | 最佳性能组合 |

### 10.7 与旧版独立插件的对比

| 特性 | 旧版独立 .so 插件 | 新版 ROS Bridge |
|------|:-----------:|:-----------:|
| 新增 Topic | 手写 C++ 插件 + 重编译 | 加一行 YAML 配置 |
| ROS Service 支持 | ❌ 不支持 | ✅ bytes 通用透传 |
| ROS Action 支持 | ❌ 不支持 | ✅ Goal/Feedback/Result |
| 图像/点云 | ❌ TCP 逐帧拷贝 | ✅ SHM 三缓冲零拷贝 |
| ROS Master 重连 | ❌ 手动处理 | ✅ 自动检测恢复 |
| 配置修改 | 改代码 | 改 YAML，重启即生效 |

> 旧版插件（`apps/plugins/ros/`）仍保留作为教学参考，但不建议在新项目中使用。详见 [DEPRECATED.md](plugins/ros/DEPRECATED.md)。

### 10.8 编译部署

**在机器人/仿真环境（有 ROS）上**：

```bash
cd CoreX && ./build.sh
# CMake 自动检测 ROS → 编译 librrosbridge.a + CoreXDaemon (HAS_ROS_BRIDGE=1)
```

**在云服务器（无 ROS）上**：

```bash
cd CoreX && ./build.sh
# CMake 跳过 ROS Bridge → CoreXDaemon 正常编译，仅无 Bridge 功能
# 提示: "ROS NOT found — skipping CoreX-ROS Bridge"
```

**交叉编译场景**：在开发机上编译，部署到机器人：

```bash
# 开发机（有 ROS SDK 但未必有 roscore）
cd CoreX
source /opt/ros/melodic/setup.bash   # 加载 ROS 环境
./build.sh                            # 编译 Bridge
scp build/CoreXDaemon robot@ip:/opt/corex/
```

### 10.9 常见问题

**Q: 启动时提示 "ROS Master not reachable"？**

正常现象。Bridge 会在 CoreXDaemon 启动时尝试连接 ROS Master。确保先启动 `roscore`，或者在 `corex_daemon.yaml` 中设置 `master_uri`。

**Q: 如何添加新的 Topic 映射？**

编辑 `corex_daemon.yaml` 的 `ros_bridge.topics` 段，添加新条目，重启 CoreXDaemon 即可。无需重编译。

**Q: Bridge 模块的性能开销？**

ROS Bridge 本身常驻内存 < 50MB（不含图像缓冲）。Topic 缓存读取 + 序列化延迟 < 100μs。对非 Bridge 的 RPC 请求无任何性能影响。

**Q: 支持 ROS 2 吗？**

当前版本仅支持 ROS 1（Melodic/Noetic）。ROS 2 的支持计划在后续版本中添加（`rclcpp` + DDS）。

**Q: Bridge 启动失败但 CoreXDaemon 正常运行？**

Bridge 失败不会阻塞 CoreXDaemon 启动。原有的 RPC 服务（插件加载的 MathService 等）完全不受影响。检查 ROS 日志定位 Bridge 问题。

### 10.10 更多信息

| 文档 | 路径 |
|------|------|
| Bridge 模块文档 | [../src/ros_bridge/README.md](../src/ros_bridge/README.md) |
| Proto 定义 | [../proto/robot_service.proto](../proto/robot_service.proto) |
| Proto 定义 (ROS 类型) | [../proto/ros_messages.proto](../proto/ros_messages.proto) |
| 遥控器源码 | [examples/robot_controller/main.cpp](examples/robot_controller/main.cpp) |
| 旧插件迁移指南 | [plugins/ros/DEPRECATED.md](plugins/ros/DEPRECATED.md) |
| AI 开发指南 | [../CLAUDE.md](../CLAUDE.md) |

---

## 九、相关文档

| 文档 | 路径 |
|------|------|
| 项目 README | [../README.md](../README.md) |
| AI 开发指南 | [../CLAUDE.md](../CLAUDE.md) |
| Bug 分析 | [BUG_ANALYSIS.md](BUG_ANALYSIS.md) |
| RPC 压测说明 | [../tests/test_rpc_benchmark.md](../tests/test_rpc_benchmark.md) |
| Echo 测试说明 | [../tests/Test_EchoServer_README.md](../tests/Test_EchoServer_README.md) |
| ROS Bridge 集成 | [← 见上方第十章](#十ros-bridge-集成corex--ros-协议网关) |
