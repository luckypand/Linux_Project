# EchoServer 测试说明

## 概述

`Test_EchoSever.cpp` 是一个基于 `CoreX` 网络库的 **双模式测试程序**，同时包含 **Echo 服务器** 和 **压力测试客户端** 两种角色。

- **Server 模式**（默认）：使用 `TcpServer` 的多线程 Reactor 模型，将接收到的客户端数据原样返回（Echo）。
- **Client 模式**（`--mode client`）：一个轻量级的高并发压力客户端，使用单线程 epoll + timerfd 管理数万个连接，支持心跳保活、内存监控和详细统计。

## 架构

### Server 模式架构

```
┌─────────────────────────────────────────────────┐
│  Main Reactor (主线程)                          │
│  ├─ EventLoop::loop() — epoll 事件循环          │
│  ├─ Acceptor — 监听 listenfd，接受新连接         │
│  └─ EventLoopThreadPool — 管理 Sub Reactor 线程 │
├─────────────────────────────────────────────────┤
│  Sub Reactor 0 (线程1)    Sub Reactor 1 (线程2)  │
│  Sub Reactor 2 (线程3)    Sub Reactor 3 (线程4)  │
│  ├─ 负责已建立连接的 IO 事件                     │
│  └─ 每个 Sub Reactor 运行独立的 EventLoop::loop()│
└─────────────────────────────────────────────────┘
```

### Client 模式架构

```
┌───────────────────────────────────────────────────┐
│  StressClient (单线程)                            │
│  ├─ epoll_create1() — 单 epoll fd 管理所有连接    │
│  ├─ timerfd — 定时心跳 + 周期统计                 │
│  ├─ Phase 1: 批量创建非阻塞 socket + connect      │
│  ├─ Phase 2: epoll_wait 事件循环                  │
│  │   ├─ EPOLLOUT → 连接建立成功                   │
│  │   ├─ EPOLLIN  → 收取 Echo 回包                │
│  │   ├─ timerfd  → 发送 PING 心跳 + 打印统计      │
│  │   └─ EPOLLERR/HUP → 连接异常断开               │
│  └─ 结束后打印 RSS + 完整统计报表                 │
└───────────────────────────────────────────────────┘
```

## 编译与运行

### 编译

项目使用 CMake 构建，可执行文件生成在 `build/` 目录下：

```bash
cd /root/Cplus/CoreX
./build.sh                    # 全量构建
# 或手动构建：
cd build && cmake .. && make  # 增量构建
```

生成的可执行文件：

| 可执行文件 | 说明 |
|-----------|------|
| `build/test_echo_server` | EchoServer 测试程序（双模式） |
| `build/test_net_integration` | 网络库集成测试 |
| `build/timeout_server_test` | 超时踢人服务器测试 |

### 运行

```bash
cd /root/Cplus/CoreX/build

# 启动服务器（默认监听 0.0.0.0:8080，4 个 Sub Reactor）
./test_echo_server

# 启动压力客户端（连接 127.0.0.1:8080，建立 10000 连接，持续 600 秒）
./test_echo_server --mode client --connections 10000 --duration 600
```


### 命令行参数

| 参数 | 默认值 | 适用模式 | 说明 |
|------|--------|---------|------|
| `--mode` | `server` | 通用 | 运行模式：`server` 或 `client` |
| `--port` | `8080` | server | 服务器监听端口 |
| `--threads` | `4` | server | Sub Reactor 线程数 |
| `--server-ip` | `127.0.0.1` | client | 目标服务器 IP |
| `--server-port` | `8080` | client | 目标服务器端口 |
| `--connections` | `10000` | client | 建立的连接数 |
| `--duration` | `600` | client | 测试持续时间（秒） |
| `--heartbeat` | `1` | client | 心跳间隔（秒） |

## 测试场景

### 场景 1：基本 Echo 功能测试

**使用 `telnet`：**

```bash
# 终端 1：启动服务器
./test_echo_server

# 终端 2：用 telnet 连接
telnet 127.0.0.1 8080

# 连接后输入任意字符，服务器会原样返回：
# Hello          ← 输入
# Hello          ← 服务器 Echo 返回
# CoreX Network  ← 输入
# CoreX Network  ← 服务器 Echo 返回
```

**使用 `nc` (netcat)：**

```bash
# 终端 1
./test_echo_server

# 终端 2
echo "Hello EchoServer" | nc 127.0.0.1 8080
# 输出: Hello EchoServer
```

### 场景 2：内存泄漏测试（万连接保活）

这是本程序的核心测试场景：启动服务器后，用 Client 模式建立大量长连接并持续发送心跳，观察服务器 RSS 是否稳定。

```bash
# 终端 1：启动服务器（后台运行）
./test_echo_server --port 8080 --threads 4 &
SERVER_PID=$!

# 终端 2：启动压力客户端（10000 连接，持续 10 分钟）
./test_echo_server --mode client \
    --server-ip 127.0.0.1 --server-port 8080 \
    --connections 10000 --duration 600 --heartbeat 1
```

**预期输出（Client 端）：**

```
[client] target 127.0.0.1:8080, connections=10000, duration=600s, heartbeat_interval=1s
[client] RSS before: 1234 KB
[client] Phase 1: creating 10000 sockets ...
[client] 10000 sockets created, entering event loop
[client] t=5s  active=9998  est=10000  fail=2  closed=0  hb=49990  rss=5678KB
[client] t=10s active=9998  est=10000  fail=2  closed=0  hb=99980  rss=5678KB
...
```

**预期输出（Server 端）：**

```
[server] listening on 0.0.0.0:8080 with 4 sub-reactor threads
[server] RSS start: 2345 KB
[server] conns=10000  msgs=50000  rss=3456KB
[server] conns=10000  msgs=100000  rss=3456KB
...
```

> **关注点**：Server 端的 RSS 应在连接建立后趋于稳定，不随时间和消息数增长，即证明无内存泄漏。

### 场景 3：并发短连接压力测试

```bash
# 终端 1
./test_echo_server

# 终端 2：100 个并发短连接
for i in $(seq 1 100); do
    (echo "msg-$i" | nc 127.0.0.1 8080 &)
done
wait
```

### 场景 4：吞吐量测试（配合 tcpkali）

```bash
# 终端 1
./test_echo_server --port 8080 --threads 4

# 终端 2
tcpkali -c 100 -T 60 127.0.0.1:8080
```

## 预期现象

| 测试场景 | 行为 | 现象 |
|---------|------|------|
| 单客户端连接 | 连接建立，Sub Reactor 注册读事件 | 客户端可正常收发 |
| 客户端发送数据 | `handleRead` → `messageCallback_` → Echo 回写 | 客户端收到相同内容 |
| 客户端断开 | `handleClose` → `closeCallback_` → `removeConnection` | 服务器清理连接资源 |
| 多客户端并发 | 每个连接分配到不同 Sub Reactor | 无锁竞争，高吞吐处理 |
| 万连接保活（Client 模式） | 批量非阻塞 connect → epoll 事件驱动 | 连接稳定，RSS 不增长 |
| 心跳保活 | timerfd 定时发送 `PING\n` | 服务器 Echo 返回，连接不因空闲断开 |
| 信号退出 | Ctrl+C 触发 SIGINT | 两端优雅关闭，打印最终统计 |

## 代码流程

### Server 端

```
main()
  └─ EventLoop loop                    // 创建主 Reactor
  └─ EchoServer server(&loop, ...)     // 创建 TcpServer
      └─ server_.setThreadNum(4)       // 设置 4 个子线程
      └─ server_.setConnectionCallback // 注册连接回调
      └─ server_.setMessageCallback    // 注册消息回调
  └─ server.start()                    // 启动线程池 + 开始监听
  └─ loop.loop()                       // 进入主事件循环

新连接到达时：
  Acceptor::handleRead()
    └─ TcpServer::newConnection()
        └─ 选择一个 Sub Reactor
        └─ 创建 TcpConnection
        └─ subloop->RunInloop([conn](){ conn->connectEstablished(); })
            └─ connectionCallback_(shared_from_this())  // 通知用户

数据到达时：
  TcpConnection::handleRead()
    └─ inBuffer.readFd()               // 读入数据
    └─ messageCallback_(shared_from_this(), inBuffer)  // 回调用户
    └─ EchoServer::onMessage()         // 用户回调中 Echo

连接关闭时：
  TcpConnection::handleClose()
    └─ closeCallback_(shared_from_this())
    └─ TcpServer::removeConnection()
        └─ removeConnectionInLoop()
            └─ subloop->SengToPending([conn](){ conn->connectDestroyed(); })
```

### Client 端

```
StressClient::run()
  ├─ bump_fd_limit()                   // 调大文件描述符上限
  ├─ epoll_create1()                   // 创建 epoll
  ├─ timerfd_create()                  // 创建定时器
  ├─ Phase 1: 批量连接
  │   └─ for i in 0..num_conn:
  │       ├─ socket(SOCK_NONBLOCK)
  │       ├─ setsockopt(TCP_NODELAY, SO_SNDBUF, SO_RCVBUF)
  │       ├─ connect() → EINPROGRESS
  │       └─ epoll_ctl(EPOLL_CTL_ADD, EPOLLOUT)
  ├─ Phase 2: 事件循环
  │   └─ epoll_wait()
  │       ├─ EPOLLOUT → getsockopt(SO_ERROR) → 连接建立 → 改监听 EPOLLIN
  │       ├─ EPOLLIN  → read() 收取 Echo 数据
  │       ├─ timerfd  → send(PING) 心跳 + 每 5s 打印统计
  │       └─ EPOLLERR/HUP → 关闭连接
  └─ print_final_stats()              // 打印完整测试报告
```

## 线程模型

- **Server 端**：
  - **Main Reactor（主线程）**：负责 accept 新连接，然后将连接分发给 Sub Reactor
  - **Sub Reactor（4 个子线程）**：每个线程运行独立的 `EventLoop::loop()`，负责已建立连接的 IO 读写
  - **Round-Robin 分发**：新连接按顺序轮流分配到各个 Sub Reactor，实现负载均衡
  - **Watcher 线程**：每 10 秒打印连接数、消息数和 RSS，收到信号后通知主循环退出

- **Client 端**：
  - **单线程 epoll**：一个线程管理所有连接，使用非阻塞 IO + epoll 事件驱动
  - **timerfd 驱动**：定时心跳发送和周期性统计输出
  - **无锁设计**：单线程无需任何锁同步

## 关键设计点

### 连接生命周期管理（Client）

```cpp
struct Conn {
    int  fd;          // socket fd，-1 表示已关闭
    bool connected;   // TCP 握手是否完成
};
```

- Phase 1 批量创建 socket 并发起非阻塞 `connect()`
- Phase 2 通过 epoll 事件驱动完成握手
- `EPOLLOUT` 事件 → `getsockopt(SO_ERROR)` 确认连接成功
- 成功后切换为 `EPOLLIN` 监听 Echo 回包

### 内存监控

通过读取 `/proc/self/status` 中的 `VmRSS` 字段实现实时内存监控，Server 端每 10 秒、Client 端每 5 秒输出一次 RSS，用于判断是否存在内存泄漏。

### 信号安全退出

- `SIGINT`/`SIGTERM` → 设置 `g_running = 0`
- Server 端：watcher 线程检测到后调用 `loop.quit()` 退出事件循环
- Client 端：主循环检测到后调用 `client.stop()` 退出 epoll 循环
