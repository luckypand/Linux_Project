# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
./build.sh                  # Release build, all targets
./build.sh debug            # Debug build, all targets
./build.sh asan             # Debug + AddressSanitizer, all targets
./build.sh <target>         # Release build, single target
./build.sh asan <target>    # ASan build, single target
./build.sh clean            # Remove build directory
```

Targets: `test_echo_server`, `timeout_server_test`, `test_net_integration`.

Build outputs go to `build/`. ASan builds append `_ASAN` to the binary name.

C++17 required. Dependencies: pthreads. Protobuf is used by the RPC module (`src/rpc/`) but is **not yet wired into CMakeLists.txt** — the RPC sources and generated `.pb.cc` files are not part of any build target.

## Architecture

### Reactor Network Library (`src/net/`)

The core module. Thread model: main-Reactor + sub-Reactor pool (one EventLoop per thread, round-robin dispatch). Pattern follows muduo.

```
Acceptor (main loop) ──accept──> TcpServer::newConnection
  → choose sub-Reactor (round-robin via EventLoopThreadPool)
  → create TcpConnection, bind user callbacks, dispatch to sub-loop
```

**Callback chain (bottom-up):**

1. `Poller::poll()` returns active fds → `Channel` event callbacks (Read/Write/Error/Close)
2. `TcpConnection` registers its `handleRead/handleWrite/handleError/handleClose` as Channel callbacks
3. `TcpConnection` invokes user-facing callbacks: `messageCallback_`, `connectionCallback_`, `closeCallback_`, `writeCompleteCallback_`
4. `TcpServer` bridges: users register callbacks on TcpServer → TcpServer forwards them to each TcpConnection in `newConnection()`

**Key classes:**
- `EventLoop` — per-thread event loop wrapping `epoll` + `eventfd` for cross-thread wakeup. Has a `pendingFunctors_` queue (`RunInloop` / `SendToPending`) for cross-thread task dispatch.
- `Poller` — thin `epoll` wrapper with dynamic event array resizing. Channels cycle through states: `kNEW → KADDED → KDELETED → kNEW`.
- `Channel` — binds an fd + its IO event callbacks. Does not own the fd.
- `TcpConnection` — owns a `Socket` and a `Channel`. Uses `enable_shared_from_this` for safe lifetime management. Has 4 states: `kConnencting → kConnected → kDisconnecting → kDisconnected`.
- `TcpServer` — owns an `Acceptor` and an `EventLoopThreadPool`. Maintains a `connections_` map.
- `HeapTimer` — min-heap of `TimeNode`, keyed by `fd`. Used for idle connection timeout. Ref map `[fd → heap_index]` for O(log n) adjust.
- `Buffer` — vector-backed read/write buffer with prependable reserve space (like muduo Buffer). Uses `readv` with stack extrabuf for efficient scatter-gather IO.

### RPC Module (`src/rpc/`)

Layered on top of the net module. Wires protobuf-based RPC over TCP using a dynamic service adapter pattern — individual RPC methods are dispatched via protobuf's `ServiceDescriptor`/`MethodDescriptor` metadata without compile-time knowledge of concrete message types.

**Framing protocol:** 8-byte header (4-byte magic `0x42414E41` big-endian + 4-byte payload length big-endian) followed by serialized protobuf payload. Max single message: 64 MB.

**Key classes:**
- `RpcCodec` — TCP stream → framed messages. Reads from `Buffer`, validates magic/length, extracts complete payloads, then calls `businessCallback_` with the deserialized bytes. Magic constant: `0x4241E41`.
- `RpcServer` — owns a `TcpServer` and an `RpcCodec`. Wires `TcpServer::messageCallback` → `RpcCodec::Onmessage` → `RpcServer::handleRpcCodecMessage`. Maintains a `dispatchTable_` mapping service names → `RpcServiceAdapter*`. Hardcodes 4 worker threads.
- `RpcServiceAdapter` — Wraps a protobuf `ServiceDescriptor` for dynamic dispatch. Uses `MethodDescriptor` to dynamically instantiate request/response messages via `MessageFactory::GetPrototype()`, deserializes the payload, calls the registered handler, and serializes the response — all without knowing concrete message types at compile time.
- `MathServiceImpl` — Example concrete adapter inheriting `RpcServiceAdapter`. Registers `Add` and `Sub` handlers that cast to `MathRequest`/`MathResponse` and perform arithmetic.

**Data flow:**
```
TCP bytes → Buffer → RpcCodec::Onmessage (framing/validation)
  → businessCallback_ → RpcServer::handleRpcCodecMessage (parse RpcMessage envelope)
  → dispatchTable_[service] → RpcServiceAdapter::dispatch (dynamic deserialize → handler → serialize)
  → sendResponse (wrap in RpcMessage envelope + TLV header + conn->send)
```

### Proto Definitions (`proto/`)

- `rpc_message.proto` — RPC envelope: `RpcMessage` with type (REQUEST/RESPONSE/ERROR), correlation id, service/method names, payload bytes, and error codes.
- `math_service.proto` — Example service definition: `MathService` with `Add`/`Sub` RPCs, plus `MathRequest`/`MathResponse` messages.

Generated `.pb.h`/`.pb.cc` files are checked in alongside the `.proto` sources.

### Log Module (`src/log/`)

Singleton async logger. Macros: `LOG_DEBUG/INFO/WARN/ERROR/FATAL`. Routes to per-module log files based on `__FILE__`.

### Placeholder Directories

`src/ipc/` and `src/websocket/` exist but are empty — planned for shared-memory IPC and WebSocket support respectively.

## Known Issues

### Critical
- **Magic number mismatch between RpcCodec and RpcServer**: [RpcCodec.hpp:5](src/rpc/RpcCodec.hpp#L5) defines `RPC_MAGIC_NUMBER = 0x4241E41` but [RpcServer.cpp:67](src/rpc/RpcServer.cpp#L67) sends `0x42414E41` ("BANA"). The codec that validates incoming frames expects a different magic than what the server sends out. Any RPC response will be rejected by a peer running the same codec.

### Moderate
- **writeCompleteCallback never invoked**: `writeCompleteCallback_` is stored (settable via `TcpConnection::setwriteCompleteCallback` and `TcpServer::setWriteCompleteCallback`) but never called in `TcpConnection::handleWrite()`. After the output buffer is fully drained, the user is not notified.
- **TcpServer::closeCallback_ and writeCompleteCallback_ unused**: Both stored in [TcpServer.hpp:43-44](src/net/TcpServer.hpp#L43-L44) with setters, but never forwarded to connections via `newConnection()` and never invoked.
- **TcpConnection::handleError silently calls handleClose()**: [TcpConnection.cpp:85-89](src/net/TcpConnection.cpp#L85-L89) — error conditions result in silent connection close with no error notification to the user.
- **`RpcCodec::Onmessage` peek-based magic read**: [RpcCodec.cpp:7](src/rpc/RpcCodec.cpp#L7) — `peekInt32()` reads a full 4-byte int32 but `RPC_MAGIC_NUMBER = 0x4241E41` is only 28 significant bits. The leading zero byte matters for wire format compatibility with the server's `htonl(0x42414E41)`.
- **`sendErrorReasponse` typo**: Method name "Reasponse" should be "Response" — consistently misspelled in both [RpcServer.hpp:24](src/rpc/RpcServer.hpp#L24) and [RpcServer.cpp:89](src/rpc/RpcServer.cpp#L89), as well as at the call site on [RpcServer.cpp:33](src/rpc/RpcServer.cpp#L33).

### Minor
- **Unused `RpcMethodCallback` typedef**: `using RpcMethodCallback = std::function<void()>;` in [RpcServer.hpp:11](src/rpc/RpcServer.hpp#L11) is unused — the old per-method callback approach was replaced by `RpcServiceAdapter`.
- **RPC module not in build**: `src/rpc/` sources and `proto/*.pb.cc` are not listed in `CMakeLists.txt` — the RPC module doesn't compile as part of any target.
- **`rpcMsg.set_payload(rstPayload)` in sendResponse**: [RpcServer.cpp:62](src/rpc/RpcServer.cpp#L62) — the `//?` comment suggests uncertainty about whether the serialized inner response should be set as the envelope payload (it is correct per the framing protocol).

## Tests

Tests are bare `assert()`-based, not GTest. They all double as integration tests since they compile the full net module:

| Test | What it covers |
|------|---------------|
| `test_net_integration` | Buffer, Socket options, Acceptor smoke, Channel callbacks, EventLoop cross-thread dispatch, echo server end-to-end |
| `test_echo_server` | TcpServer stress test: server mode (echo with stats) and client mode (epoll-based concurrent connector with heartbeat) |
| `timeout_server_test` | HeapTimer idle-timeout: single-thread server that kicks clients after 5s of silence |

Run a test after building:
```bash
./build/test_net_integration
./build/test_echo_server --mode server --port 8080 &
./build/test_echo_server --mode client --connections 1000 --duration 10
./build/timeout_server_test
```

## Code Style

- C++17, no exceptions in the net module (errors handled via return values / callbacks).
- Chinese comments throughout — keep them when editing nearby code.
- Class names PascalCase, methods camelCase, members snake_case with trailing underscore.
- No clang-format config; match the surrounding style.

## 语言风格
- 除了专门的技术名词外，优先使用中文回答用户问题

## 代码编码
- 代码中有UTF-8与GB 2312两种风格，请先检查具体风格再进行代码和注释的修改，避免乱码