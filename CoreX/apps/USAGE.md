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
│   └── corex_daemon.yaml               # 默认配置模板（端口、线程数、IPC、插件路径、日志）
│
├── examples/                           # 示例
│   ├── math_plugin/                    # 业务插件示例（Add/Sub 计算服务）
│   │   ├── math_service_plugin.cpp     #   继承 RpcServiceAdapter 实现业务逻辑
│   │   ├── CMakeLists.txt              #   独立编译脚本
│   │   └── README.md                   #   插件编译/部署说明
│   └── python_client/                  # Python RPC 客户端示例
│       └── rpc_client.py               #   零依赖，手写 Protobuf Wire Format 编解码
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
| `examples/python_client/` | 演示任意语言如何通过 TCP + Protobuf 调用 CoreX RPC。Python 版零外部依赖。 | 客户端开发者 |
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

## 九、相关文档

| 文档 | 路径 |
|------|------|
| 项目 README | [../README.md](../README.md) |
| AI 开发指南 | [../CLAUDE.md](../CLAUDE.md) |
| Bug 分析 | [BUG_ANALYSIS.md](BUG_ANALYSIS.md) |
| RPC 压测说明 | [../tests/test_rpc_benchmark.md](../tests/test_rpc_benchmark.md) |
| Echo 测试说明 | [../tests/Test_EchoServer_README.md](../tests/Test_EchoServer_README.md) |
