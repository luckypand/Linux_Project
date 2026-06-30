# CoreX — C++17 网络库 & Protobuf RPC 框架

一个基于 Reactor 模式的 Linux C++17 网络库，参考 muduo 架构设计，集成了自定义 Protobuf RPC 框架和共享内存 IPC 快速通道。

**两种使用模式**：
- 🔧 **嵌入式库模式** — 头文件 + 静态库 `librpc.a`，编译进工程代码
- 🚀 **守护进程模式** — `CoreXDaemon` 独立服务进程，插件化加载业务 `.so`（类比 ROS `roscore`）

## 目录结构

```
CoreX/
├── CMakeLists.txt                  # CMake 构建脚本
├── README.md                       # 项目说明
├── build.sh                        # 一键编译脚本
├── CLAUDE.md                       # AI 辅助开发指南
│
├── proto/                          # Protobuf 协议定义
│   ├── rpc_message.proto           # RPC 信封格式（REQUEST/RESPONSE/ERROR + 时间戳）
│   ├── rpc_message.pb.h/.cc        # 生成代码
│   ├── math_service.proto          # 示例服务定义（MathService.Add/Sub）
│   └── math_service.pb.h/.cc       # 生成代码
│
├── src/                            # 核心源码（详见下方模块架构）
│   ├── net/                        # 模块一：Reactor 网络引擎（11 个类）
│   ├── ipc/                        # 模块二：共享内存 IPC（3 个类）
│   ├── rpc/                        # 模块三：RPC 框架层（6 个类）
│   └── log/                        # 模块四：异步日志
│
├── apps/                           # ★ 守护进程封装层（独立于 src/，不修改核心代码）
│   ├── daemon/                     # 守护进程源代码
│   │   ├── CoreXDaemon.hpp/.cpp    #   守护进程主类（生命周期管理）
│   │   ├── ConfigManager.hpp/.cpp  #   YAML 配置解析器
│   │   ├── PluginLoader.hpp/.cpp   #   .so 插件动态加载器（dlopen）
│   │   ├── SignalHandler.hpp/.cpp  #   SIGTERM/SIGINT 信号处理 + 优雅关闭
│   │   └── main.cpp                #   入口点
│   ├── config/
│   │   └── corex_daemon.yaml       #   默认配置文件模板
│   ├── examples/
│   │   ├── math_plugin/            #   业务插件示例（编译为 .so）
│   │   └── python_client/          #   Python RPC 客户端示例
│   ├── systemd/
│   │   └── corex-daemon.service    #   systemd unit 模板
│   ├── USAGE.md                    #   ★ 使用文档（守护进程 + 嵌入式库两种模式）
│   └── BUG_ANALYSIS.md             #   已知 Bug 分析与修复指南
│
└── tests/                          # 测试 & 文档
    ├── Test_EchoSever.cpp          # Echo 服务器/客户端压测
    ├── Test_EchoServer_README.md   # Echo 测试说明
    ├── test_net_integration.cpp    # 网络库集成测试
    ├── test_net_integration.md     # 集成测试说明
    ├── test_rpc_benchmark.cpp      # RPC 多线程压测（支持 IPC fast-path）
    ├── test_rpc_benchmark.md       # RPC 压测详细说明
    ├── timeout_server_test.cpp     # 超时踢连接测试
    └── perf_flamegraph_rpc.sh      # 火焰图一键生成脚本
```

## 模块架构

```
┌──────────────────────────────────────────────────┐
│                    RPC 框架层                      │
│  RpcServer → RpcCodec → RpcServiceAdapter         │
│  IpcRpcBridge ←→ IpcEndpoint (共享内存 IPC)        │
├──────────────────────────────────────────────────┤
│                  网络引擎层                        │
│  TcpServer → TcpConnection → Channel → Poller     │
│  EventLoop + EventLoopThreadPool                  │
├──────────────────────────────────────────────────┤
│                   基础设施层                       │
│  IPC (ShmRingBuffer + ShmMemoryPool)              │
│  Log  (异步日志)                                   │
│  Timer (HeapTimer 小顶堆)                         │
└──────────────────────────────────────────────────┘
```

## 快速开始

### 模式一：守护进程模式（推荐，类比 ROS roscore）

适用于生产环境部署，支持多语言客户端、插件热加载。

```bash
cd CoreX

# 1. 全量构建（含 CoreXDaemon + math_service 插件）
./build.sh

# 2. 启动守护进程
./build/CoreXDaemon --config apps/config/corex_daemon.yaml

# 3. 使用 Python 客户端调用 RPC
python3 apps/examples/python_client/rpc_client.py add --a 3 --b 5
# → Result: 8  Latency: 0.28 ms

python3 apps/examples/python_client/rpc_client.py sub --a 10 --b 3
# → Result: 7  Latency: 0.27 ms

# 4. 后台运行 / 优雅关闭
./build/CoreXDaemon --config apps/config/corex_daemon.yaml --daemon
kill -TERM $(pidof CoreXDaemon)   # 优雅关闭：等待请求完成 → 清理 SHM → 卸载插件
```

详细使用说明请参阅 [apps/USAGE.md](apps/USAGE.md)。

### 模式二：嵌入式库模式

适用于 C++ 工程内嵌集成，直接调用 API。

```bash
cd CoreX

# 构建
./build.sh test_rpc_benchmark

# RPC 压测（自动检测 127.0.0.1 → 走共享内存 IPC 加速）
./build/test_rpc_benchmark --calls 10000 --threads 2

# 独立服务器模式
./build/test_rpc_benchmark --mode server --port 8080

# Echo 服务器压测
./build/test_echo_server --mode server --port 8080 &
./build/test_echo_server --mode client --connections 1000 --duration 10

# 网络库集成测试
./build/test_net_integration

# 超时踢连接测试
./build/timeout_server_test
```

### 构建变体

```bash
# 全部构建（Release，含 CoreXDaemon）
./build.sh

# 单个目标
./build.sh test_rpc_benchmark

# 开启 RPC 耗时测量 + 日志打印
./build.sh timestamp=1 log=1 test_rpc_benchmark

# ASan 构建
./build.sh asan

# Debug 构建
./build.sh debug
```

构建产物在 `build/` 目录。可执行文件命名规则：`<目标名>[功能后缀][_ASAN]`，如 `test_rpc_benchmark`、`test_rpc_benchmark-log-timestamp`。
插件 `.so` 输出到 `build/plugins/`。

## 核心特性

### 1. Reactor 网络引擎

- **one loop per thread** + 主从 Reactor 模式
- 主 Reactor 负责 accept，子 Reactor（默认 4 个）轮询分发连接
- epoll 事件驱动 + eventfd 跨线程唤醒
- 自定义 Buffer 解决 TCP 粘包/半包问题

### 2. Protobuf RPC 框架

- 自定义 **TLV 帧协议**：魔术字 `0x42414E41`（"BANA"）+ 4 字节长度 + Protobuf 负载
- 动态服务适配器（基于 `ServiceDescriptor`/`MethodDescriptor`），无需编译期感知具体消息类型
- 延迟监控：P50/P90/P95/P99/P999 分位数 + 阶段分解（上行/处理/下行）
- `ENABLE_TIMESTAMP` / `ENABLE_LOG` 编译期功能开关，发布时零开销关闭

### 3. 共享内存 IPC Fast-Path ★

当 RPC 客户端检测到目标 IP 为 `127.0.0.1` 或 `localhost` 时，**自动跳过 TCP 协议栈**，改用共享内存环形缓冲区直通：

```
客户端 send() ──→ requestRing.CAS_Push() ──→ eventfd 通知
                                                      ↓
服务端 epoll_wake ←── IpcRpcBridge ←── EventLoop ←───┘
       ↓
   dispatch → sendResponse → OutputFunc → responseRing.CAS_Push()
                                                      ↓
客户端 recv() ←── eventfd 通知 ←─────────────────────┘
```

- 双向 RingBuffer（请求环 + 响应环），每块 4096 字节，16 槽位
- CAS 无锁块状态机：`FREE → WRITING → READY → FREE`
- 服务端通过 `IpcRpcBridge` 无缝接入 EventLoop
- 虚拟 TcpConnection（socketpair 内核）挂载 `OutputFunc`，响应经 RingBuffer 回传
- 与 TCP 路径**共享 100% 的编解码和分发管线**

**性能对比**（本机 loopback）：

| 指标 | TCP 路径 | IPC Fast-Path |
|------|----------|---------------|
| 中位数 RTT | 50–200 μs | ~50 μs |
| 吞吐量 | ~20K calls/s | ~14K+ calls/s |
| 延迟稳定性 | 受内核调度影响 | 更稳定、抖动更小 |

### 4. 构建系统

- CMake 支持 Release/Debug/ASan 三种构建模式
- 单目标构建、增量编译
- Protobuf 自动集成
- 火焰图脚本一键生成（`perf record` → FlameGraph SVG）

## 依赖

| 依赖 | 说明 |
|------|------|
| C++17 | 语言标准（`std::optional`、`if constexpr` 等） |
| CMake ≥ 3.8 | 构建系统 |
| Protobuf ≥ 3.6 | RPC 序列化框架 |
| yaml-cpp ≥ 0.6 | 配置文件解析（仅 CoreXDaemon 需要） |
| pthreads | 多线程 |
| librt | POSIX 共享内存（`shm_open`/`shm_unlink`） |
| libdl | 动态库加载（`dlopen`/`dlsym`，仅 CoreXDaemon 需要） |
| Linux ≥ 4.x | epoll、eventfd、shared mem |

### 安装依赖（Ubuntu/Debian）

```bash
sudo apt install -y build-essential cmake protobuf-compiler libprotobuf-dev \
                    libyaml-cpp-dev libssl-dev
```

## 两种模式对比

| 维度 | 守护进程模式 | 嵌入式库模式 |
|------|:-----------:|:-----------:|
| 多语言客户端 | ✅ TCP + Protobuf | ❌ 仅 C++17 |
| 部署方式 | 独立进程，systemd 管理 | 编译进宿主进程 |
| 业务扩展 | 插件 .so 热加载 | 编译时注册 |
| 进程隔离 | ✅ 崩溃不影响宿主 | ❌ 共享进程空间 |
| 调用延迟 | TCP ~50–200μs / IPC ~50μs | 函数调用级 |
| 适用场景 | 生产环境、多服务架构 | 嵌入式、高性能单进程 |
