# RPC Benchmark 压测工具说明

## 概述

`test_rpc_benchmark` 是一个多线程 RPC 客户端压测工具，用于验证 CoreX RPC 协议栈的正确性和性能。

**核心测试场景：**
- 在本机（127.0.0.1）发起 **10 万次连续 RPC 同步调用**（`MathService.Add`）
- 调用严格遵守 CoreX 自研的 TLV 帧协议（魔术字 + 长度 + Protobuf 负载）
- 每轮调用测量 **RTT（往返延迟）**，生成完整的延迟分布统计

**达标红线：**
1. **零协议错误** —— 不允许任何一次半包解析错误或魔术字校验失败
2. **单次 RTT ≤ 500 微秒** —— 所有样本的往返延迟必须压缩在 0.5ms 以内
3. **火焰图验证** —— CPU 热点集中在 Protobuf 序列化/反序列化，而非网络锁竞争

---

## 前置条件

| 依赖 | 版本要求 | 说明 |
|------|---------|------|
| C++ | C++17+ | CMake 构建必需 |
| Protobuf | 3.6.1+ | 序列化框架 |
| pthreads | 任意 | 多线程支持 |
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
```

构建产物：`build/test_rpc_benchmark`

---

## 使用方式

### 1. Benchmark 模式（默认）

内嵌 RPC 服务器 + 多线程客户端，自动完成压测并输出统计报告：

```bash
# 默认参数：2 线程，10 万次调用，RTT 红线 500 μs
./build/test_rpc_benchmark

# 自定义线程数和调用次数
./build/test_rpc_benchmark --threads 4 --calls 50000

# 调整 RTT 红线
./build/test_rpc_benchmark --calls 200000 --max-rtt-us 600
```

### 2. Server 模式（独立服务器）

用于配合 `perf` 进行 CPU 采样分析：

```bash
# 启动独立服务器
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
| `--server-ip` | string | `127.0.0.1` | 服务器 IP 地址 |
| `--help` | flag | - | 打印帮助信息 |

---

## 架构说明

### 线程模型

```
┌─ 主线程 ────────────────────────────────────────────────────┐
│  1. 启动内嵌服务器线程                                        │
│  2. 创建 N 个客户端线程（发令枪同步启动）                      │
│  3. 收集统计 → 停止服务器 → 打印报告                          │
└─────────────────────────────────────────────────────────────┘

┌─ 服务器线程 ────────────────────────────────────────────────┐
│  EventLoop → RpcServer (4 个 sub-Reactor) → MathServiceImpl  │
│  处理连接、帧解码、方法分发、响应发送                          │
└─────────────────────────────────────────────────────────────┘

┌─ 客户端线程 0 ──┐ ┌─ 客户端线程 1 ──┐     ┌─ 客户端线程 N ──┐
│ 阻塞 socket (TCP_NODELAY)            │  ...  │                 │
│ for i in 0..M:                       │       │                 │
│   send(request)  ─┐                  │       │                 │
│   recv(response) ←┤ RTT 计时区间      │       │                 │
│   校验魔数/结果    │                  │       │                 │
│   记录 RTT ──────┘                  │       │                 │
└──────────────────┘ └─────────────────┘       └─────────────────┘
```

### 数据流

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

CoreX RPC 使用自研的 **TLV 帧协议**（Type-Length-Value）：

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

### RpcMessage 信封结构（Protobuf）

```protobuf
message RpcMessage {
    MessageType type     = 1;  // REQUEST=0, RESPONSE=1, ERROR=2
    fixed64     id       = 2;  // 请求/响应关联 ID
    string      service  = 3;  // 服务名，如 "CoreX.rpc.MathService"
    string      method   = 4;  // 方法名，如 "Add"
    bytes       payload  = 5;  // 内层业务消息的序列化字节
    ErrorCode   error    = 6;  // 错误码（仅 ERROR 类型使用）
}
```

---

## 性能指标

### 预期延迟分布（本机 loopback）

| 指标 | 预期值（Release 构建） | 说明 |
|------|----------------------|------|
| Min RTT | 10-30 μs | 最佳情况，无排队 |
| Avg RTT | 20-50 μs | 受 Protobuf 序列化开销主导 |
| Median (P50) | 20-40 μs | 典型调用延迟 |
| P99 | 50-200 μs | 含偶尔的内核调度抖动 |
| Max | < 500 μs | 红线要求 |
| 吞吐量 (2 线程) | 50,000-150,000 calls/s | 以总成功调用 / 墙钟时间计算 |

### 为什么红线设 500 μs？

500 μs（0.5 ms）是一个经过谨慎选择的阈值：

1. **远高于正常值**：loopback 环境下 Protobuf 序列化 + TCP 往返的正常延迟在 20-50 μs，500 μs 预留了 **10x 的抖动裕量**
2. **识别异常**：超过此阈值说明存在锁竞争、上下文切换风暴、或内存分配抖动
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
1. 以 `perf record` 启动服务器（99 Hz 采样，DWARF 调用栈展开）
2. 启动客户端压测（10 万次调用）
3. 客户端完成后，SIGINT 停止服务器，触发 `perf.data` 写入
4. 调用 `perf script` → `stackcollapse-perf.pl` → `flamegraph.pl` 生成 SVG

输出文件：`rpc_benchmark_flamegraph.svg`

### 手动火焰图分析

如果想自己控制采样参数：

```bash
# 终端 1: 以 perf 启动服务器
sudo perf record -F 99 -g --call-graph dwarf -o /tmp/perf_rpc.data \
    ./build/test_rpc_benchmark --mode server --port 8080

# 终端 2: 运行客户端
./build/test_rpc_benchmark --mode benchmark --port 8080 --calls 100000

# 终端 1: Ctrl+C 停止服务器，然后生成火焰图
sudo perf script -i /tmp/perf_rpc.data | \
    /root/Cplus/FlameGraph/stackcollapse-perf.pl | \
    /root/Cplus/FlameGraph/flamegraph.pl \
        --title "CoreX RPC Benchmark - CPU Hotspots" \
        --width 1200 \
        --colors java \
    > rpc_benchmark_flamegraph.svg
```

### 火焰图解读要点

打开 SVG 后，关注以下关键信号：

**✅ 期望的热点（证明架构正确）：**
- `google::protobuf::Message::SerializeToString()` —— 占较宽区域
- `google::protobuf::Message::ParseFromString()` —— 占较宽区域
- `RpcServiceAdapter::dispatch()` —— 分发逻辑
- `TcpConnection::send()` / `TcpConnection::handleRead()` —— 网络 IO

**❌ 不希望的热点（需要优化）：**
- `pthread_mutex_lock` / `__lll_lock_wait` —— 锁竞争
- `epoll_wait` 占比过高 —— IO 等待（正常会有一定比例）
- `malloc` / `operator new` 占主导 —— 内存分配瓶颈
- 内核空间 (`[kernel]`) 占比过高 —— 系统调用开销

**面试级判断标准：** 如果 Protobuf 序列化/反序列化合计占 CPU 采样 50%+，而锁相关占比 < 5%，即可证明"CPU 热点在业务逻辑而非框架锁竞争"。

---

## 故障排查

### 编译错误

| 现象 | 原因 | 解决 |
|------|------|------|
| `google/protobuf/... No such file` | Protobuf 未安装 | `apt install libprotobuf-dev protobuf-compiler` |
| `undefined reference to protobuf::...` | 链接缺少 -lprotobuf | 确认 CMakeLists.txt 中 `find_package(Protobuf REQUIRED)` 和 `target_link_libraries(... rpc)` |
| `RpcServer.hpp: No such file` | include 路径未配置 | 确认 rpc 库的 `target_include_directories` 已设置 PUBLIC |

### 运行时错误

| 现象 | 原因 | 解决 |
|------|------|------|
| `connect() 失败: Connection refused` | 服务器未就绪 | 确认服务器线程已 `set_value()` 就绪信号 |
| `魔数校验失败: 期望 0x42414E41` | 魔术字不匹配 | 确保 `RpcCodec.hpp` 和 `RpcServer.cpp` 中魔术字统一为 `0x42414E41` |
| `MathResponse 反序列化失败` | proto 定义不一致 | 重新生成 `.pb.cc/.pb.h`：`protoc --cpp_out=. proto/*.proto` |
| RTT 超过 500 μs | 系统负载高或调度抖动 | 减少 `--threads`，关闭其他进程，重新测试 |
| `RpcMessage 反序列化失败` | 半包或协议不匹配 | 检查 `recv_n()` 是否正确接收了完整帧 |

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
| `tests/test_rpc_benchmark.cpp` | 压测主程序（服务器 + 客户端） |
| `tests/test_rpc_benchmark.md` | 本文档 |
| `tests/perf_flamegraph_rpc.sh` | 火焰图一键生成脚本 |
| `src/rpc/RpcCodec.hpp` | RPC 帧编解码器 |
| `src/rpc/RpcCodec.cpp` | RPC 帧解析实现 |
| `src/rpc/RpcServer.hpp` | RPC 服务器封装 |
| `src/rpc/RpcServer.cpp` | RPC 服务器实现 |
| `src/rpc/RpcServiceAdapter.hpp` | 动态服务适配器 |
| `src/rpc/MathServiceImpl.hpp` | 示例 MathService 实现 |
| `proto/rpc_message.proto` | RPC 信封 Proto 定义 |
| `proto/math_service.proto` | MathService Proto 定义 |
| `CMakeLists.txt` | CMake 构建配置 |
| `build.sh` | 构建脚本 |
