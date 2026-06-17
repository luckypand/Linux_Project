# RPC Benchmark 压测工具说明

## 概述

`test_rpc_benchmark` 是一个多线程 RPC 客户端压测工具，用于验证 CoreX RPC 协议栈的正确性和性能。

**核心测试场景：**
- 在本机（127.0.0.1）发起 **10 万次连续 RPC 同步调用**（`MathService.Add`）
- 调用严格遵守 CoreX 自研的 TLV 帧协议（魔术字 + 长度 + Protobuf 负载）
- 每轮调用测量 **RTT（往返延迟）**，生成完整的延迟分布统计
- **★ 新增功能：** 当 `--server-ip` 为 `127.0.0.1` 或 `localhost` 时，**自动检测并启用 IPC fast-path**，通过共享内存 RingBuffer 直通，跳过内核 TCP 协议栈

**达标红线：**
1. **零协议错误** —— 不允许任何一次半包解析错误或魔术字校验失败
2. **单次 RTT ≤ 500 微秒** —— 所有样本的往返延迟必须压缩在 0.5ms 以内（IPC 路径通常 < 100 μs）
3. **火焰图验证** —— CPU 热点集中在 Protobuf 序列化/反序列化，而非网络锁竞争

---

## 前置条件

| 依赖 | 版本要求 | 说明 |
|------|---------|------|
| C++ | C++17+ | CMake 构建必需 |
| Protobuf | 3.6.1+ | 序列化框架 |
| pthreads | 任意 | 多线程支持 |
| librt | 任意 | POSIX 共享内存（`shm_open`/`shm_unlink`），IPC fast-path 必需 |
| Linux perf | 5.4+ | 火焰图采样（可选） |
| FlameGraph | Brendan Gregg 版本 | SVG 火焰图生成（可选，位于 `/root/Cplus/FlameGraph/`） |

---

## 构建

```bash
cd /root/Cplus/CoreX

# Release 构建（推荐：含调试符号，perf 可正确展开函数名）
./build.sh release test_rpc_benchmark

# Debug 构建
./build.sh debug test_rpc_benchmark

# ASan 构建（调试内存问题）
./build.sh asan test_rpc_benchmark

# 开启 RPC 耗时测量 + 日志打印
./build.sh timestamp=1 log=1 test_rpc_benchmark
```

构建产物：`build/test_rpc_benchmark`（开启功能开关时带后缀，如 `build/test_rpc_benchmark-log-timestamp`）

---

## 使用方式

### 1. Benchmark 模式（默认）

内嵌 RPC 服务器 + 多线程客户端，自动完成压测并输出统计报告：

```bash
# 默认参数：2 线程，10 万次调用，RTT 红线 500 μs
# 127.0.0.1 自动启用 IPC fast-path（共享内存加速）
./build/test_rpc_benchmark

# 自定义线程数和调用次数
./build/test_rpc_benchmark --threads 4 --calls 50000

# 调整 RTT 红线
./build/test_rpc_benchmark --calls 200000 --max-rtt-us 600

# 通过 TCP 连接其他机器（不会启用 IPC fast-path）
./build/test_rpc_benchmark --server-ip 192.168.1.100 --port 8080
```

### 2. Server 模式（独立服务器）

用于配合 `perf` 进行 CPU 采样分析。**Server 模式默认启用 IPC fast-path 监听**（TCP + IPC 双通道）：

```bash
# 启动独立服务器（监听 TCP 端口 + 共享内存 IPC）
./build/test_rpc_benchmark --mode server --port 8080

# 另一个终端运行客户端压测
./build/test_rpc_benchmark --mode benchmark --port 8080
```

### 3. 命令行参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--mode` | string | `benchmark` | 运行模式：`benchmark`（压测）或 `server`（独立服务器） |
| `--port` | int | `8080` | 服务器监听端口 |
| `--threads` | int | `hardware_concurrency` | 客户端线程数（每个线程一个持久连接） |
| `--calls` | int | `100000` | 总 RPC 调用次数（均分到各线程） |
| `--max-rtt-us` | double | `500.0` | RTT 达标红线 / 微秒 |
| `--server-ip` | string | `127.0.0.1` | 服务器 IP 地址；设为 `127.0.0.1` 或 `localhost` 时**自动启用 IPC fast-path** |
| `--help` | flag | - | 打印帮助信息 |

---

## 架构说明

### IPC Fast-Path 数据流 ★

```
客户端 (run_client_thread)                        服务端 (IpcRpcBridge + EventLoop)
══════════════════════════                        ═══════════════════════════════════

build_packet() [TLV header + Protobuf payload]
    │
    ├── serverIp == 127.0.0.1 ?
    │       │ YES
    │       ▼
    │   ┌── IPC Fast-Path ────────────────────────────────────────────┐
    │   │                                                              │
    │   │  IpcEndpoint("/corex_rpc_ipc", ATTACH)                       │
    │   │       │                                                      │
    │   │       ▼                                                      │
    │   │  requestRing.CAS_Push(wirePacket)                            │
    │   │       │                                                      │
    │   │       ▼                                                      │
    │   │  ::write(eventfd, 1)    ────────────────►  epoll_wait 唤醒    │
    │   │                                             │                │
    │   │                                      IpcRpcBridge::onIpcSignal()
    │   │                                             │                │
    │   │                                      requestRing.CAS_Pop()    │
    │   │                                             │                │
    │   │                                      Buffer → RpcCodec::Onmessage()
    │   │                                             │                │
    │   │                                      handleRpcCodecMessage()  │
    │   │                                             │                │
    │   │                                      dispatch → sendResponse()│
    │   │                                             │                │
    │   │                                      ipcConn→send() → OutputFunc
    │   │                                             │                │
    │   │                                      responseRing.CAS_Push()  │
    │   │                                             │                │
    │   │  ::read(eventfd)    ◄──────────────── ::write(eventfd)       │
    │   │       │                                                      │
    │   │       ▼                                                      │
    │   │  responseRing.CAS_Pop(respPacket)                            │
    │   │       │                                                      │
    │   │       ▼                                                      │
    │   │  解析 TLV → 验证魔数 → 反序列化 → 校验结果                   │
    │   └──────────────────────────────────────────────────────────────┘
    │
    │   serverIp != 127.0.0.1 ?
    │       │ YES (TCP 路径)
    │       ▼
    │   socket::send() ────► TCP 协议栈 ────► epoll ────► TcpConnection::handleRead()
    │   socket::recv() ◄─── TCP 协议栈 ◄─── conn->send() ◄── RpcServer
    │
    ▼
 记录 RTT / 阶段分解 / 超时统计
```

### 线程模型

```
┌─ 主线程 ────────────────────────────────────────────────────┐
│  1. 启动内嵌服务器线程（含 IpcRpcBridge 若 IP=127.0.0.1）    │
│  2. 预热连接（消除 JIT/Cache 冷启动）                        │
│  3. 创建 N 个客户端线程（发令枪同步启动）                      │
│  4. 收集统计 → 停止服务器 → 打印报告                          │
└─────────────────────────────────────────────────────────────┘

┌─ 服务器线程 ────────────────────────────────────────────────┐
│  EventLoop → RpcServer (4 个 sub-Reactor) → MathServiceImpl  │
│  IpcRpcBridge → IpcEndpoint (监听 eventfd + RingBuffer)       │
│  处理连接、帧解码、方法分发、响应发送（TCP + IPC 双通道）      │
└─────────────────────────────────────────────────────────────┘

┌─ 客户端线程 0 ──┐ ┌─ 客户端线程 1 ──┐     ┌─ 客户端线程 N ──┐
│ [127.0.0.1? YES] │ │                  │     │                  │
│ IPC: push→ring   │ │ TCP: send/recv   │     │ ...              │
│ spin-wait resp   │ │ RTT 计时区间      │     │                  │
│ 校验魔数/结果    │ │                  │     │                  │
│ 记录 RTT ───────┘ │                  │     │                  │
└──────────────────┘ └─────────────────┘     └──────────────────┘
```

### TCP 路径数据流

```
客户端                                              服务器
────▶ send ────────────────────────────────────▶
[4B 魔数 "BANA" BE]
[4B 负载长度 BE]
[RpcMessage 序列化字节]
  type    = REQUEST
  id      = 自增计数器
  service = "CoreX.rpc.MathService"
  method  = "Add"
  payload = [MathRequest 序列化字节]
               a = ..., b = ...

                        RpcCodec::Onmessage()  ──▶  校验魔数 + 帧长度
                        RpcServer::handleRpcCodecMessage()
                          ├─ 解析 RpcMessage
                          ├─ dispatchTable_[service] → MathServiceImpl::dispatch()
                          │    ├─ 反序列化 MathRequest
                          │    ├─ handler: result = a + b
                          │    └─ 序列化 MathResponse
                          └─ sendResponse()
                               ├─ 封装 RpcMessage (type=RESPONSE)
                               ├─ 序列化 + TLV 头部
                               └─ conn->send()

◀─── recv ─────────────────────────────────────
[4B 魔数 "BANA" BE]      ◀── 与请求相同的魔数
[4B 负载长度 BE]
[RpcMessage 序列化字节]
  type    = RESPONSE
  payload = [MathResponse 序列化字节]
               result  = a + b
               success = true
```

---

## 线格式协议

CoreX RPC 使用自研的 **TLV 帧协议**（Type-Length-Value），TCP 和 IPC 路径共用完全相同的线格式：

```
字节 0-3:  魔术字 (uint32, 大端序)
            固定值 0x42414E41 ("BANA" in ASCII)
字节 4-7:  负载长度 (uint32, 大端序)
            后续 Protobuf 负载的字节数
字节 8+:   负载 (bytes)
            RpcMessage 的 Protobuf 序列化结果
            最大单帧: 64 MB
```

**重要特性：**
- 魔术字 `0x42414E41`（"BANA"）用于帧边界识别，防止误解析
- 长度字段限制最大帧为 64 MB，防止内存攻击
- 所有多字节字段使用**网络字节序（大端）**—— `htonl()` 发送，`ntohl()` 接收
- 半包场景：如果收到的数据不足一帧，`RpcCodec` 会等待完整数据到达后再解析
- **IPC fast-path 使用相同的 TLV 格式**，客户端构建包和服务端解析包的逻辑与 TCP 路径完全一致

### RpcMessage 信封结构（Protobuf）

```protobuf
message RpcMessage {
    MessageType type     = 1;  // REQUEST=0, RESPONSE=1, ERROR=2
    fixed64     id       = 2;  // 请求/响应关联 ID
    string      service  = 3;  // 服务名，如 "CoreX.rpc.MathService"
    string      method   = 4;  // 方法名，如 "Add"
    bytes       payload  = 5;  // 内层业务消息的序列化字节
    ErrorCode   error    = 6;  // 错误码（仅 ERROR 类型使用）

    // ★ 时间戳追踪（仅 ENABLE_TIMESTAMP 构建时有效）
    uint64      client_send_ts   = 10;  // 客户端发送时刻 (μs, steady_clock)
    uint64      server_recv_ts   = 11;  // 服务端反序列化完成时刻
    uint64      server_send_ts   = 12;  // 服务端响应封装完成时刻
}
```

---

## 性能指标

### IPC Fast-Path 延迟分布（本机 loopback，Release 构建）

| 指标 | 预期值 | 说明 |
|------|--------|------|
| Min RTT | 10–30 μs | 无排队，纯 RingBuffer 操作 |
| Avg RTT | 30–70 μs | 受 Protobuf 序列化开销主导 |
| Median (P50) | 20–50 μs | 典型调用延迟 |
| P99 | 50–200 μs | 含偶尔的 CAS 自旋 + 调度抖动 |
| Max | < 500 μs | 红线要求 |
| 吞吐量 | 10K–20K calls/s | 以总成功调用 / 墙钟时间计算 |

### TCP 路径延迟分布（本机 loopback）

| 指标 | 预期值 | 说明 |
|------|--------|------|
| Min RTT | 50–100 μs | 含 TCP 协议栈处理 |
| Avg RTT | 100–200 μs | 含内核上下文切换 |
| Median (P50) | 80–150 μs | 典型 TCP 往返 |
| P99 | 200–500 μs | 含系统调用抖动 |
| Max | < 500 μs | 红线要求 |
| 吞吐量 | 15K–50K calls/s | TCP 路径下高吞吐 |

### IPC vs TCP 对比

| 维度 | TCP | IPC Fast-Path |
|------|-----|---------------|
| 数据路径 | 用户态 → 内核 → epoll → 用户态 | 用户态 RingBuffer（零拷贝） |
| 上下文切换 | 每次 send/recv 两次切换 | 仅 eventfd 通知一次 |
| 内存拷贝 | 至少 2 次（protobuf → socket buffer → protobuf） | 1 次 memcpy 到 RingBuffer |
| 协议开销 | TCP 头 + IP 头 | 无（纯 TLV over RingBuffer） |
| 自动启用条件 | 始终可用 | `--server-ip` = `127.0.0.1` 或 `localhost` |

### 为什么红线设 500 μs？

500 μs（0.5 ms）是一个经过谨慎选择的阈值：

1. **远高于正常值**：loopback 环境下 IPC fast-path 的正常延迟在 20–50 μs，TCP 路径在 50–150 μs，500 μs 预留了 **10x 的抖动裕量**
2. **识别异常**：超过此阈值说明存在锁竞争、上下文切换风暴、或 RingBuffer 满导致的自旋等待
3. **面试级标准**：足以向面试官证明延迟的稳定性

---

## 火焰图分析

### 生成火焰图

使用配套的 `perf_flamegraph_rpc.sh` 脚本一键生成：

```bash
cd /root/Cplus/CoreX
sudo ./tests/perf_flamegraph_rpc.sh
```

脚本执行流程：
1. 构建 `test_rpc_benchmark`（Release + 调试符号）
2. 直接在 benchmark 模式上运行 `perf record`（99 Hz 采样，DWARF 调用栈展开），内嵌服务器 + 多线程客户端在同一进程内采样
3. benchmark 结束后 `perf.data` 自动写入
4. 调用 `perf script` → `stackcollapse-perf.pl` → `flamegraph.pl` 生成 SVG

输出文件：
- `perf_data/perf_rpc_YY-MM-DD-HHMM.data` — perf 采样原始数据
- `svg/rpc_benchmark_flamegraph_YY-MM-DD-HHMM.svg` — 火焰图 SVG

### 手动火焰图分析

如果想自己控制采样参数：

```bash
# 终端 1: 以 perf 启动服务器
sudo perf record -F 99 -g --call-graph dwarf -o perf_data/perf_rpc_$(date +%y-%m-%d-%H%M).data \
    ./build/test_rpc_benchmark --mode server --port 8080

# 终端 2: 运行客户端
./build/test_rpc_benchmark --mode benchmark --port 8080 --calls 100000

# 终端 1: Ctrl+C 停止服务器，然后生成火焰图
sudo perf script -i perf_data/perf_rpc_XX.data | \
    /root/Cplus/FlameGraph/stackcollapse-perf.pl | \
    /root/Cplus/FlameGraph/flamegraph.pl \
        --title "CoreX RPC Benchmark - CPU Hotspots" \
        --width 1200 \
        --colors java \
    > svg/rpc_benchmark_flamegraph_$(date +%y-%m-%d-%H%M).svg
```

### 火焰图解读要点

打开 SVG 后，关注以下关键信号：

**✅ 期望的热点（证明架构正确）：**
- `google::protobuf::Message::SerializeToString()` —— 占较宽区域
- `google::protobuf::Message::ParseFromString()` —— 占较宽区域
- `RpcServiceAdapter::dispatch()` —— 分发逻辑
- `TcpConnection::send()` / `TcpConnection::handleRead()` —— 网络 IO（TCP 路径）
- `ShmRingBuffer::CAS_Push()` / `CAS_Pop()` —— 无锁操作（IPC 路径）
- `IpcEndpoint::sendRequest()` / `recvResponse()` —— IPC 传输

**❌ 不希望的热点（需要优化）：**
- `pthread_mutex_lock` / `__lll_lock_wait` —— 锁竞争
- `epoll_wait` 占比过高 —— IO 等待（正常会有一定比例）
- `malloc` / `operator new` 占主导 —— 内存分配瓶颈
- 内核空间 (`[kernel]`) 占比过高 —— 系统调用开销
- `std::this_thread::yield` 占比高 —— RingBuffer 满导致的自旋（考虑增容）

**面试级判断标准：** 如果 Protobuf 序列化/反序列化合计占 CPU 采样 50%+，而锁相关占比 < 5%，即可证明"CPU 热点在业务逻辑而非框架锁竞争"。

---

## 故障排查

### 编译错误

| 现象 | 原因 | 解决 |
|------|------|------|
| `google/protobuf/... No such file` | Protobuf 未安装 | `apt install libprotobuf-dev protobuf-compiler` |
| `undefined reference to protobuf::...` | 链接缺少 -lprotobuf | 确认 CMakeLists.txt 中 `find_package(Protobuf REQUIRED)` 和 `target_link_libraries(... rpc)` |
| `RpcServer.hpp: No such file` | include 路径未配置 | 确认 rpc 库的 `target_include_directories` 已设置 PUBLIC |
| `undefined reference to shm_open` | 未链接 librt | 确认 CMakeLists.txt 中 `target_link_libraries(rpc ... rt)` |

### 运行时错误

| 现象 | 原因 | 解决 |
|------|------|------|
| `connect() 失败: Connection refused` | 服务器未就绪 | 确认服务器线程已 `set_value()` 就绪信号 |
| `魔数校验失败: 期望 0x42414E41` | 魔术字不匹配 | 确保 `RpcCodec.hpp` 和 `RpcServer.cpp` 中魔术字统一为 `0x42414E41` |
| `MathResponse 反序列化失败` | proto 定义不一致 | 重新生成 `.pb.cc/.pb.h`：`protoc --cpp_out=. proto/*.proto` |
| RTT 超过 500 μs | 系统负载高或调度抖动 | 减少 `--threads`，关闭其他进程，重新测试 |
| `RpcMessage 反序列化失败` | 半包或协议不匹配 | 检查 `recv_n()` 是否正确接收了完整帧 |
| `IPC recvResponse 超时` | RingBuffer 阻塞或服务端未启动 IPC | 检查共享内存段 `/dev/shm/corex_rpc_ipc` 是否存在，确认服务端已调用 `enableIpc()` |
| `shm_open() 失败: Permission denied` | `/dev/shm` 权限不足 | 检查 `/dev/shm` 挂载点和权限 |

### perf 相关问题

| 现象 | 原因 | 解决 |
|------|------|------|
| `perf.data: Permission denied` | `perf_event_paranoid=3` | 使用 `sudo perf record ...` |
| 火焰图中函数名显示为地址 | 缺少调试符号 | 确保用 Release（含 `-g`）或 Debug 构建 |
| `flamegraph.pl: command not found` | FlameGraph 工具不在 PATH | 使用绝对路径 `/root/Cplus/FlameGraph/flamegraph.pl` |

---

## 文件清单

| 文件 | 说明 |
|------|------|
| `tests/test_rpc_benchmark.cpp` | 压测主程序（服务器 + 客户端，含 IPC fast-path） |
| `tests/test_rpc_benchmark.md` | 本文档 |
| `tests/perf_flamegraph_rpc.sh` | 火焰图一键生成脚本 |
| `src/rpc/RpcCodec.hpp` | RPC 帧编解码器（TCP 和 IPC 共用） |
| `src/rpc/RpcCodec.cpp` | RPC 帧解析实现 |
| `src/rpc/RpcServer.hpp` | RPC 服务器封装（含 `enableIpc()`） |
| `src/rpc/RpcServer.cpp` | RPC 服务器实现（含 IPC 桥接器初始化） |
| `src/rpc/RpcServiceAdapter.hpp` | 动态服务适配器 |
| `src/rpc/MathServiceImpl.hpp` | 示例 MathService 实现 |
| `src/rpc/RpcLatencyStats.hpp` | 延迟统计收集器（P50/P99/P999 + 阶段分解） |
| `src/rpc/IpcRpcBridge.hpp` | ★ IPC 桥接器头文件 |
| `src/rpc/IpcRpcBridge.cpp` | ★ IPC 桥接器实现（eventfd → EventLoop → RingBuffer） |
| `src/ipc/IpcEndpoint.hpp` | ★ 双向共享内存 IPC 端点 |
| `src/ipc/IpcEndpoint.cpp` | ★ IPC 端点实现 |
| `src/ipc/ShmSegment.hpp` | POSIX 共享内存 RAII 封装 |
| `src/ipc/ShmSegment.cpp` | 共享内存实现 |
| `src/ipc/RingBuffer.hpp` | 无锁 MPSC 环形缓冲区 |
| `src/ipc/RingBuffer.cpp` | 环形缓冲区实现 |
| `src/net/TcpConnection.hpp` | TCP 连接管理（含 `OutputFunc` 插件） |
| `src/net/TcpConnection.cpp` | TCP 连接实现 |
| `src/net/TcpServer.hpp` | TCP 服务器封装 |
| `src/net/TcpServer.cpp` | TCP 服务器实现 |
| `src/net/Channel.hpp` | Channel 事件分发 |
| `proto/rpc_message.proto` | RPC 信封 Proto 定义 |
| `proto/math_service.proto` | MathService Proto 定义 |
| `CMakeLists.txt` | CMake 构建配置（含 IPC_SOURCES） |
| `build.sh` | 构建脚本 |
