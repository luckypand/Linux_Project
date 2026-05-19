# 网络库整体集成测试文档

## 概述

此测试套件对 `CoreX/src/net/` 目录中的所有网络库组件进行整体集成测试，验证 **Buffer**、**Socket**、**Channel**、**EventLoop**、**TcpConnection** 等模块的正确性和互操作性。

## 测试内容

### 测试 1: Buffer 基本功能

**目的**: 验证缓冲区的读写操作

**测试项目**:
- ? `append()` 和 `peek()` — 写入数据并零拷贝读取
- ? `retrieve()` — 消费指定字节数
- ? `retrieveAsString()` — 以字符串形式提取所有可读数据
- ? 多次 `append()` — 连续写入多个数据块
- ? `clear()` — 清空缓冲区

**预期结果**: 所有断言通过，数据完整性保证

---

### 测试 2: Socket 创建和配置

**目的**: 验证 Socket 包装类的基本功能

**测试项目**:
- ? Socket 对象创建 — RAII 风格管理文件描述符
- ? TCP 选项配置:
  - `setReuseAddr(true)` — 解决 TIME_WAIT 端口占用
  - `setReusePort(true)` — 支持内核级负载均衡
  - `setTcpNoDelay(true)` — 禁用 Nagle 算法
  - `setKeepAlive(true)` — 启用 TCP 心跳保活

**预期结果**: Socket 创建和配置成功，无异常

---

### 测试 3: EventLoop 基本操作

**目的**: 验证事件循环的运行和任务调度

**测试项目**:
- ? 线程 ID 检查 — `IsInloopthread()`
- ? 待处理任务队列 — `SengToPending()` 和执行
- ? 事件循环启动/退出 — `loop()` 和 `quit()`

**工作流程**:
1. 创建 EventLoop 实例
2. 向待处理任务队列添加回调函数
3. 在独立线程中启动 `loop()`
4. 等待任务执行完成
5. 调用 `quit()` 停止循环
6. 验证回调函数已执行

**预期结果**: 待处理任务正确执行，EventLoop 正常启停

---

### 测试 4: 回显服务器集成测试

**目的**: 验证完整的服务器工作流 — 从 Socket 绑定到数据通信

**工作流程**:
1. **服务器端**:
   - 创建并配置监听 Socket
   - 绑定到本地环回地址（127.0.0.1）
   - 系统自动分配端口（避免固定端口冲突）
   - 创建 Channel 管理 Socket 和事件
   - 启动 EventLoop 处理客户端连接

2. **客户端**（在主线程）:
   - 连接到服务器
   - 发送测试数据 `"Hello, Server!"`
   - 接收服务器的回显
   - 验证回显数据正确

3. **回调链路**:
   ```
   客户端连接 → 监听 Channel 就绪
           ↓
   accept() → 创建客户端 Channel
           ↓
   客户端数据到达 → 客户端 Channel 读回调
           ↓
   读取数据 → 直接写回（回显）
           ↓
   客户端接收
   ```

**预期结果**:
- ? 服务器成功绑定和监听
- ? 客户端成功连接
- ? 数据正确回显（客户端接收的数据等于发送的数据）
- ? 事件循环正确驱动 Channel 回调

---

## 编译和运行

### 编译

```bash
cd /root/Cplus/CoreX

# 清理旧构建
make clean

# 编译（需要配置 CMakeLists.txt 或 Makefile 支持此测试）
make

# 或使用 CMake
cmake -B build
cd build
make
```

### 运行测试

```bash
# 运行整体集成测试
./build/test_net_integration

# 或如果编译到其他位置
./tests/test_net_integration
```

### 预期输出

```
╔════════════════════════════════════════════════╗
║       网络库整体集成测试 (Test Suite)         ║
╚════════════════════════════════════════════════╝

=== Test 1: Buffer 基本功能 ===
[PASS] append 和 peek
[PASS] retrieve 消费字节
[PASS] retrieveAsString
[PASS] 多次 append
[PASS] clear
[?] Buffer 测试通过

=== Test 2: Socket 创建和配置 ===
[PASS] Socket 创建
[PASS] Socket 选项配置
[?] Socket 测试通过

=== Test 3: EventLoop 基本操作 ===
[PASS] IsInloopthread 检查
[PASS] 待处理任务执行
[?] EventLoop 测试通过

=== Test 4: 回显服务器集成测试 ===
[PASS] Socket 绑定
[PASS] Socket 监听
[INFO] 服务器监听端口: 12345
[PASS] Channel 和 EventLoop 绑定
[INFO] 客户端已连接，fd=5
[PASS] 客户端连接
[INFO] 回显 14 字节给 fd=5
[PASS] 数据正确回显
[?] 回显服务器测试通过

╔════════════════════════════════════════════════╗
║              ? 全部测试通过！                 ║
╚════════════════════════════════════════════════╝
```

---

## 测试覆盖范围

| 模块 | 功能 | 覆盖情况 |
|------|------|--------|
| **Buffer** | 读写、追加、消费、清空 | ? 完整 |
| **Socket** | 创建、配置 TCP 选项 | ? 基础 |
| **Channel** | 事件管理、读回调 | ? 完整 |
| **EventLoop** | 循环启停、任务调度 | ? 基础 |
| **TcpConnection** | (已有专门的超时服务器测试) | 参见 `timeout_server_test.cpp` |
| **Poller** | epoll 事件轮询 | ? 间接覆盖 |
| **Acceptor** | (可选扩展) | 已在回显服务器中体现 |

---

## 故障排查

### 问题 1: 端口绑定失败

**原因**: 指定端口已被占用或权限不足

**解决**: 测试使用 `htons(0)` 让操作系统自动分配端口，通常不会出现此问题

---

### 问题 2: EventLoop 线程未启动

**原因**: 线程启动延迟或 CPU 资源不足

**解决**: 增加 `std::this_thread::sleep_for()` 延迟，给线程更多启动时间

---

### 问题 3: 客户端连接超时

**原因**: 服务器未正确加入 epoll 或 accept() 失败

**解决**: 检查 `loop.UpdateChannel()` 是否被正确调用

---

## 扩展建议

### 1. 测试 TcpConnection 类

```cpp
void testTcpConnection()
{
    // 测试连接状态转换: kConnecting → kConnected → kDisconnecting → kDisconnected
    // 测试发送/接收数据
    // 测试回调链（connectionCallback, messageCallback, closeCallback）
}
```

### 2. 并发测试

```cpp
void testConcurrentConnections()
{
    // 模拟多个客户端同时连接和发送数据
    // 验证 EventLoop 处理并发事件的能力
}
```

### 3. 压力测试

```cpp
void testHighThroughput()
{
    // 大量数据传输
    // 测试 Buffer 扩容和 EventLoop 性能
}
```

### 4. 错误处理测试

```cpp
void testErrorHandling()
{
    // 测试连接异常断开
    // 测试读/写失败处理
    // 测试资源泄漏
}
```

---

## 参考文献

- `src/net/Buffer.hpp` — 缓冲区接口
- `src/net/Socket.hpp` — Socket 包装接口  
- `src/net/Channel.hpp` — 事件管理接口
- `src/net/EventLoop.hpp` — 事件循环接口
- `src/net/TcpConnection.hpp` — 连接管理接口
- `tests/timeout_server_test.cpp` — 超时服务器示例

---

## 维护说明

- **最后更新**: 2026-05-16
- **测试框架**: 仅使用 C++11 标准库和系统调用，无外部依赖
- **目标**: 验证网络库的核心功能和组件互操作性
