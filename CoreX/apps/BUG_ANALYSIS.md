# CoreX 已知 Bug 分析

本文档列出了 CoreX 项目中已知的 Bug，提供**触发场景**和**修复方向**供参考。
所有 Bug 均位于 `src/` 目录下，本文档不修改任何现有代码。

---

## Bug 1: writeCompleteCallback 从未被调用

**文件**: [src/net/TcpConnection.cpp:159-177](src/net/TcpConnection.cpp#L159-L177)

**问题**: `TcpConnection::handleWrite()` 在输出缓冲区完全排空后，没有调用 `writeCompleteCallback_`。用户设置了发送完成回调但永远收不到通知。

**触发场景**:

```cpp
// 启动 echo server
EventLoop loop;
TcpServer server(&loop, "0.0.0.0", 8080, "TestServer");
server.setWriteCompleteCallback([](auto conn) {
    printf("Write complete!\n");  // ← 这行永远不会输出！
});
// ... 启动服务，发送大块数据使输出缓冲区满 ...
```

具体步骤：
1. 启动 EchoServer，设置 `writeCompleteCallback`
2. 客户端发送大量数据导致服务端 `outBuffer` 积压（触发 `enableWriting`）
3. 当 `handleWrite()` 逐步写完后排空 `outBuffer`
4. **预期**: 调用 `writeCompleteCallback_` 通知用户
5. **实际**: 静默完成，回调从未被调用

**修复方向**:

在 `TcpConnection::handleWrite()` 中，`outBuffer.ReadBytes() == 0` 的分支里，添加回调调用：

```cpp
// 在 TcpConnection.cpp:167 附近，outBuffer.ReadBytes() == 0 分支内
if (writeCompleteCallback_) {
    writeCompleteCallback_(shared_from_this());
}
```

---

## Bug 2: TcpServer::closeCallback_ 和 writeCompleteCallback_ 未转发给连接

**文件**: [src/net/TcpServer.cpp:54-88](src/net/TcpServer.cpp#L54-L88)（newConnection 方法）

**问题**: `TcpServer` 存储了 `closeCallback_` 和 `writeCompleteCallback_`，并且提供了 setter 方法，但在 `newConnection()` 中创建新连接时，这两个回调没有被转发给 `TcpConnection`。

**触发场景**:

```cpp
EventLoop loop;
TcpServer server(&loop, "0.0.0.0", 8080, "TestServer");

// 用户设置关闭回调（期望连接关闭时被调用）
server.setConnectionCallback([](auto conn) {
    printf("Connected: %s\n", ...);
});
// closeCallback 在 TcpServer 上有 setter，但 newConnection 里没有转发它
// 所以这个回调存储在 TcpServer 对象上，但永远不会被调用

server.start();
loop.loop();
```

具体步骤：
1. 启动 TcpServer，设置 `closeCallback`（通过 setter，代码编译通过）
2. 客户端连接后断开
3. **预期**: `closeCallback` 被调用（通知用户连接断开）
4. **实际**: 回调存储在 `TcpServer` 成员中但从未被使用，连接断开静默发生

**修复方向**:

在 `TcpServer::newConnection()` 中，将 `closeCallback_` 转发给新建的连接：

```cpp
// 在 TcpServer.cpp:67-69 附近
conn->setConnectionCallback(connectionCallback_);
conn->setMessageCallback(messageCallback_);
conn->setwriteCompleteCallback(writeCompleteCallback_);
// 添加以下两行：
conn->setCloseCallback(closeCallback_);              // ← 缺失
// 注意：closeCallback 已被 setCloseCallback 覆盖为 removeConnection 的 lambda
// 正确的做法是先保存用户的 closeCallback，在 removeConnection 中先调用用户的再清理
```

**注意**: 这个修复需要仔细设计。当前 `closeCallback` 在 `newConnection()` 中被直接覆盖为 `removeConnection` 的 lambda。正确做法是在 `removeConnection` 的 lambda 中先调用用户的 `closeCallback_`，再执行清理逻辑。

---

## Bug 3: TcpConnection::handleError 静默关闭连接

**文件**: [src/net/TcpConnection.cpp:90-94](src/net/TcpConnection.cpp#L90-L94)

**问题**: `handleError()` 在发生 IO 错误时直接调用 `handleClose()`，没有任何错误通知或日志输出。用户无法感知连接异常断开。

**触发场景**:

```cpp
// 客户端连接后，服务端网线被拔掉 / 防火墙阻断 / TCP RST
// → epoll 返回 EPOLLERR
// → Channel 调用 errorCallback → TcpConnection::handleError()
// → 直接调用 handleClose()
// → 用户通过 closeCallback 知道连接断开了，但不知道为什么断开
```

具体步骤：
1. 建立 TCP 连接
2. 模拟网络故障（例如：客户端进程被 kill -9，或使用 iptables 阻断）
3. 服务端 epoll 检测到 EPOLLERR 事件
4. **预期**: 用户收到错误通知（错误码、错误描述）
5. **实际**: `handleError()` 直接调用 `handleClose()`，静默关闭，用户无法区分正常关闭和异常断开

**修复方向**:

在 `handleError()` 中添加错误码获取和日志输出，理想情况下通知用户：

```cpp
void TcpConnection::handleError()
{
    // 获取 socket 错误码
    int sockErr = 0;
    socklen_t optlen = sizeof(sockErr);
    if (::getsockopt(socket_->fd(), SOL_SOCKET, SO_ERROR, &sockErr, &optlen) == 0) {
        // 记录错误码，可用于日志或通知用户
        // LOG_ERROR("TcpConnection error on fd=%d: %s", socket_->fd(), strerror(sockErr));
    }
    // 可选：增加 errorCallback_ 通知用户
    handleClose();
}
```

---

## Bug 4: sendErrorReasponse 拼写错误

**文件**: 
- [src/rpc/RpcServer.hpp:38-39](src/rpc/RpcServer.hpp#L38-L39)（声明）
- [src/rpc/RpcServer.cpp:76-78](src/rpc/RpcServer.cpp#L76-L78)（调用）
- [src/rpc/RpcServer.cpp:219](src/rpc/RpcServer.cpp#L219)（TIMESTAMP 分支）
- [src/rpc/RpcServer.cpp:247](src/rpc/RpcServer.cpp#L247)（非 TIMESTAMP 分支）

**问题**: 方法名拼写错误：`sendErrorReasponse` 应为 `sendErrorResponse`（少了一个字母 'e'，多了一个字母 'a'）。

**触发场景**:

编译不受影响（只是命名问题），但在 IDE 中：
- 代码补全时输入 `sendErrorResponse` 找不到方法
- grep 搜索 `sendErrorResponse` 返回零结果
- 新开发者阅读代码时需要额外注意这个拼写

**修复方向**:

全局重命名（4 处声明 + 对应调用点）：
- `sendErrorReasponse` → `sendErrorResponse`

注意需要同步修改 `.hpp` 声明和 `.cpp` 定义，以及所有调用位置。

---

## Bug 5: handleClose 在连接断开后调用 closeCallback 时状态不一致

**文件**: [src/net/TcpConnection.cpp:188-193](src/net/TcpConnection.cpp#L188-L193)

**问题**: `handleClose()` 直接将状态设为 `kDisconnected` 后调用 `closeCallback_`。但如果 `closeCallback_`（即 `TcpServer::removeConnection`）中通过 `conn->connected()` 检查状态，会返回 `false`，而此时连接的实际清理还未完成（`connectDestroyed` 还未被调用）。

**触发场景**:

```cpp
// 在 closeCallback 中检查连接状态
server.setCloseCallback([](auto conn) {
    if (conn->connected()) {  // ← 始终返回 false！
        // 这段代码永远不会执行
    }
});
```

具体步骤：
1. 客户端断开连接
2. `handleClose()` 先将 `state_` 设为 `kDisconnected`
3. 然后调用 `closeCallback_`
4. 如果回调中调用 `conn->connected()`（检查 `state_ == kConnected`），结果将是 `false`

**修复方向**:

在调用 `closeCallback_` 之后再将状态设为 `kDisconnected`，或者在 `closeCallback` 调用前先检查旧状态并增加一个新的回调语义（如 `onDisconnecting`）。

---

## 总结

| Bug | 严重程度 | 影响范围 | 修复复杂度 |
|-----|---------|---------|-----------|
| Bug 1: writeCompleteCallback 未调用 | 中 | 所有使用发送完成通知的场景 | 低 |
| Bug 2: TcpServer close/writeComplete 未转发 | 中 | 所有通过 TcpServer 设置回调的场景 | 中 |
| Bug 3: handleError 静默关闭 | 中 | 所有异常断开场景 | 低 |
| Bug 4: sendErrorReasponse 拼写 | 低 | 代码可读性 | 低 |
| Bug 5: handleClose 状态不一致 | 中 | 连接关闭回调中的状态检查 | 低 |

---

*文档生成时间: 2026-06-26*
*注意: 本文档仅供分析参考，不修改任何 src/ 下的现有代码*
