# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
./build.sh                          # Release build, all targets
./build.sh debug                    # Debug build, all targets
./build.sh asan                     # Debug + AddressSanitizer, all targets
./build.sh <target>                 # Release build, single target
./build.sh asan <target>            # ASan build, single target
./build.sh clean                    # Remove build directory
./build.sh log=1                    # Enable log printing
./build.sh timestamp=1              # Enable RPC latency instrumentation
./build.sh timestamp=1 log=1        # Both features enabled
```

Targets: `test_echo_server`, `timeout_server_test`, `test_net_integration`, `test_rpc_benchmark`, `test_ros_bridge`.

Feature flags (`timestamp=1`/`log=1`) append suffixes to binary names (e.g., `test_rpc_benchmark-log-timestamp`).

Build outputs go to `build/`. Binary naming: `FEATURE_SUFFIX` (e.g., `-log`, `-timestamp`, `-log-timestamp`) is inserted before `_ASAN` when ASan is enabled. Examples: `test_echo_server`, `test_rpc_benchmark-log-timestamp`, `timeout_server_test_ASAN`.

C++17 required. Dependencies: pthreads, Protobuf 3.6+. The RPC module (`src/rpc/`) and generated `.pb.cc` files build into a static library `librpc.a` included by `test_rpc_benchmark`.

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

### IPC Module (`src/ipc/`)

Shared-memory inter-process communication with lock-free ring buffer. Consists of two layers:

**Key classes:**
- `ShmMemoryPool` — RAII wrapper around POSIX shared memory APIs (`shm_open` + `ftruncate` + `mmap` + `shm_unlink`). Constructor creates a named shared memory segment of a given size mapped into the process address space; destructor unmaps and unlinks. Construction unlinks any pre-existing segment first to start fresh.
- `ShmRingBuffer` — Lock-free ring buffer designed for MPSC (multi-producer, single-consumer) use. Total capacity: 16 blocks of ~256 bytes each.

**Ring buffer protocol (per block):**
1. Producer: CAS-claims the next `write_index` slot → CAS state `FREE → WRITING` → memcpy data → store state `READY` (release)
2. Consumer: checks `read_index != write_index` → checks state == `READY` (acquire) → copies data → CAS state `READY → FREE` → advances `read_index`

**Block states:** `FREE (0) → WRITING (1) → READY (2) → FREE` (cycle). Each `ShmBlock` is cacheline-aligned (`alignas(64)`) to prevent false sharing. Uses C++ `std::atomic` with explicit memory ordering (`acquire`/`release`/`relaxed`).

### ROS Bridge Module (`src/ros_bridge/`)

Protocol gateway between ROS robot ecosystem and CoreX RPC framework. Converts ROS Topic/Service/Action communication models to CoreX RPC services, enabling bidirectional cloud-robot communication.

> **Build requirement:** ROS environment (catkin + roscpp). Automatically skipped (`HAS_ROS_BRIDGE` undefined) on non-ROS machines.

**Key classes:**
- `RosNodeManager` — Singleton managing `ros::init()` / `NodeHandle` / `AsyncSpinner` lifecycle.
- `BridgeConfig` — YAML-driven configuration parser: maps ROS topics/services/actions to RPC methods.
- `RosBridgeEngine` — Core engine: creates `TopicBridge`/`ServiceBridge`/`ActionBridge` instances from config, manages lifecycle.
- `DynamicServiceAdapter` — Extends `RpcServiceAdapter` with overridable `dispatch()`/`serviceName()` for config-driven adapters (no compile-time `.proto` needed).
- `TopicBridge` — Bidirectional ROS Topic ↔ RPC mapping (subscribe=cache data, publish=forward to ROS).
- `ServiceBridge` — ROS Service → RPC mapping using `ros::SerializedMessage` for type-erased bytes passthrough.
- `ActionBridge` — ROS Action → RPC mapping (Goal/Feedback/Result/Cancel split into separate RPC methods, polling for feedback).
- `ShmImageTransporter` — Triple-buffer shared memory channel for high-frequency image/pointcloud data (zero-copy on same host, falls back to protobuf bytes over network).

**Configuration** (in `corex_daemon.yaml`):
```yaml
ros_bridge:
  enabled: true
  topics:
    - ros_topic: "/cmd_vel"
      direction: "publish"
      rpc_service: "CoreX.rpc.MotionControl"
      rpc_method: "SetVelocity"
```

**Data flow:**
```
Cloud → CoreX RPC → DynamicServiceAdapter::dispatch
  → TopicBridge::handlePublish → [ShmTopicBus (同机 <10μs)] → ros::Publisher → /cmd_vel → Robot

Robot → /odom → TopicBridge::onRosMessage → [cache + ShmTopicBus push] 
  → Cloud RPC call → TopicBridge::handleGetCached → Response
```

- `ShmTopicBus` — Lock-free shared memory Topic bus. Sequence-number ring buffer with eventfd notification. Replaces TCPROS for same-machine nodes (latency: 50-200μs → < 10μs). Each topic has its own SHM segment with 16 slots, fan-out to up to 8 subscribers.

### Proto Definitions (`proto/`)

- `rpc_message.proto` — RPC envelope: `RpcMessage` with type (REQUEST/RESPONSE/ERROR), correlation id, service/method names, payload bytes, error codes, and latency-tracking timestamps (`client_send_ts`, `server_recv_ts`, `server_send_ts` in microseconds from `steady_clock`).
- `math_service.proto` — Example service definition: `MathService` with `Add`/`Sub` RPCs, plus `MathRequest`/`MathResponse` messages.
- `robot_service.proto` — Robot RPC services: `RobotTelemetry` (7 RPCs), `MotionControl` (4 RPCs), `SimulationControl` (3 RPCs), `Navigation` (4 RPCs). 30+ message types for robot data (odometry, joint states, images, laser scans, navigation goals).
- `ros_messages.proto` — ROS message Protobuf equivalents: `JointState`, `Image`, `LaserScan`, `PointCloud2`, `TFMessage`, `Imu`, `BatteryState`, generic passthrough wrappers.

Generated `.pb.h`/`.pb.cc` files are checked in alongside the `.proto` sources. Regenerate with:
```bash
cd CoreX && protoc --cpp_out=proto -Iproto proto/rpc_message.proto
```

### Log Module (`src/log/`)

Singleton async logger. Macros: `LOG_DEBUG/INFO/WARN/ERROR/FATAL`. Routes to per-module log files based on `__FILE__`.

### Placeholder Directories

`src/websocket/` exists but is empty — planned for WebSocket support.

## Known Issues

### Moderate
- **writeCompleteCallback never invoked**: `writeCompleteCallback_` is stored (settable via `TcpConnection::setwriteCompleteCallback` and `TcpServer::setWriteCompleteCallback`) but never called in `TcpConnection::handleWrite()`. After the output buffer is fully drained, the user is not notified.
- **TcpServer::closeCallback_ and writeCompleteCallback_ unused**: Both stored in [TcpServer.hpp:43-44](src/net/TcpServer.hpp#L43-L44) with setters, but never forwarded to connections via `newConnection()` and never invoked.
- **TcpConnection::handleError silently calls handleClose()**: [TcpConnection.cpp:85-89](src/net/TcpConnection.cpp#L85-L89) — error conditions result in silent connection close with no error notification to the user.
- **`RpcCodec::Onmessage` peek-based magic read**: [RpcCodec.cpp:7](src/rpc/RpcCodec.cpp#L7) — `peekInt32()` reads a full 4-byte int32 but `RPC_MAGIC_NUMBER = 0x4241E41` is only 28 significant bits. The leading zero byte matters for wire format compatibility with the server's `htonl(0x42414E41)`.
- **`sendErrorReasponse` typo**: Method name "Reasponse" should be "Response" — consistently misspelled in both [RpcServer.hpp:24](src/rpc/RpcServer.hpp#L24) and [RpcServer.cpp:89](src/rpc/RpcServer.cpp#L89), as well as at the call site on [RpcServer.cpp:33](src/rpc/RpcServer.cpp#L33).

### Minor
- **Unused `RpcMethodCallback` typedef**: `using RpcMethodCallback = std::function<void()>;` in [RpcServer.hpp:11](src/rpc/RpcServer.hpp#L11) is unused — the old per-method callback approach was replaced by `RpcServiceAdapter`.

## Tests

Tests are bare `assert()`-based, not GTest. They all double as integration tests since they compile the full net module:

| Test | What it covers |
|------|---------------|
| `test_net_integration` | Buffer, Socket options, Acceptor smoke, Channel callbacks, EventLoop cross-thread dispatch, echo server end-to-end |
| `test_echo_server` | TcpServer stress test: server mode (echo with stats) and client mode (epoll-based concurrent connector with heartbeat) |
| `test_rpc_benchmark` | RPC end-to-end: multi-threaded client → server → MathService dispatch → response, with latency stats. Supports `--mode server` and `--mode benchmark` |
| `timeout_server_test` | HeapTimer idle-timeout: single-thread server that kicks clients after 5s of silence |
| `test_ros_bridge` | ★ ROS Bridge unit tests: BridgeConfig parsing, DynamicServiceAdapter, ShmTopicBus pub/sub (11 tests) |

Run a test after building:
```bash
./build/test_net_integration
./build/test_echo_server --mode server --port 8080 &
./build/test_echo_server --mode client --connections 1000 --duration 10
./build/timeout_server_test
./build/test_rpc_benchmark --calls 1000 --threads 2
./build/test_ros_bridge
```

## Code Style

- C++17, no exceptions in the net module (errors handled via return values / callbacks).
- Chinese comments throughout — keep them when editing nearby code.
- Class names PascalCase, methods camelCase, members snake_case with trailing underscore.
- No clang-format config; match the surrounding style.

## 语言风格
- 除了专门的技术名词外，优先使用中文回答用户问题

## 代码编码
- 代码与注释的风格为UTF-8，请记住并且在修改时避免乱码