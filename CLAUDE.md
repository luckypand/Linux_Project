# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Workspace Overview

This is a **collection of independent C/C++ mini-projects** for Linux system programming — not a single application. Each directory (except `CoreX/`) is a self-contained exercise or experiment.

The **active project** is [`CoreX/`](CoreX/) — a C++17 network library (Reactor pattern, Protobuf RPC, ROS Bridge, SHM Topic Bus). Key docs:
- [CoreX/CLAUDE.md](CoreX/CLAUDE.md) — Architecture, build commands, tests, known issues
- [CoreX/DEPLOYMENT.md](CoreX/DEPLOYMENT.md) — ★ 完整部署与使用手册
- [CoreX/README.md](CoreX/README.md) — 项目概览与快速开始
- [CoreX/apps/USAGE.md](CoreX/apps/USAGE.md) — 守护进程/嵌入式/ROS Bridge 使用文档

## Build Commands

### CoreX (active project)

```bash
cd CoreX
./build.sh                  # Release build, all targets
./build.sh debug            # Debug build
./build.sh asan             # Debug + AddressSanitizer
./build.sh <target>         # Single target (test_echo_server, timeout_server_test, test_net_integration, test_rpc_benchmark)
./build.sh clean            # Remove build directory
```

### Standalone mini-projects

Each has its own Makefile. Verified commands:

```bash
cd Process_Poll && make release|debug|run|clean
cd Shell && make release|debug|run|clean
cd Exec && make run|clean
cd MuchFile && make|clean
```

## Key Conventions

- **C and C++** throughout. CoreX requires C++17; mini-projects are mostly C with some C++.
- **Chinese comments** are common — match the existing comment language when editing nearby code. Files may use UTF-8 or GB2312 encoding; check before editing to avoid corruption.
- **No root-level build** — build inside each project's directory.
- **Assert-based tests** — CoreX tests use bare `assert()`, not GTest.
- **Many Makefiles inject system headers** via compiler flags (`-include stdio.h -include unistd.h -include stdlib.h`), so check the Makefile before adding/removing includes.
- **Process-related code** follows the standard parent/child fd lifecycle: `pipe` → `fork` → close unused ends → dispatch → `waitpid`.

## Directory Map

| Directory | What |
|-----------|------|
| [`CoreX/`](CoreX/) | **Active project** — Reactor network library + Protobuf RPC framework |
| [`muduo-master/`](muduo-master/) | Reference: muduo networking library (Chen Shuo) |
| [`Process_Poll/`](Process_Poll/) | Process pool with pipe-based task dispatch |
| [`Shell/`](Shell/) | Mini shell with built-in commands |
| [`Exec/`](Exec/) | `exec*` family process replacement experiments |
| [`Shm_Process/`](Shm_Process/) | Shared-memory inter-process communication |
| [`Tinywebsocket/`](Tinywebsocket/) | WebSocket server implementation |
| [`Tinywebsocket_demo/`](Tinywebsocket_demo/) | WebSocket demo/experiment |
| [`FlameGraph/`](FlameGraph/) | Brendan Gregg's FlameGraph tools (perf visualization) |
| [`rr-5.6.0-Linux-x86_64/`](rr-5.6.0-Linux-x86_64/) | rr record-and-replay debugger |
| `Pid_ctrl/`, `Pid_demo/` | PID controller experiments |
| `Timer/` | Timer/scheduling experiments |
| `Singal/` | Signal handling experiments |
| `Redirect/` | I/O redirection experiments |
| `Myprocess/`, `MyProcess_Poll/` | Process creation/management experiments |
| `Myfile/`, `MuchFile/` | File I/O experiments; `MuchFile/` is a large-file stress test |
| `log/`, `Log/` | Logging experiments |
| `exe/` | Compiled binaries |

## Avoid Scanning

- `MuchFile/` — contains a very large number of generated files for build stress testing. Skip it unless it's the explicit target.
- `muduo-master/` — third-party reference library, not part of active development.

## 语言风格
- 除了专门的技术名词外，优先使用中文回答用户问题

## 代码编码
- 代码与注释的风格为UTF-8，请记住并且在修改时避免乱码

## 代码拓展或重构时对于代码本身
- 无论何时，在你需要对代码进行重构或者添加任意一个新模块时，你都应该先为我提供该模块的最小实现模型的核心代码以及逻辑关系，在我确定理解了你的意图后再进行修改。同时，在你希望为我提供额外的模块功能或者拓展时，你势必要先让我明白你添加其的意图和目的，除非我确认理解你的意思后允许你添加，否则你不应该为我加入额外功能的模块或将一个已有模块冗余化

## 回答风格
- 注意，在我进行模块或者相应数据流的提问时，只回答我需要的内容，不要为我提供与我的提问太多无关的部分，并且尽量将与数据流相关的问题以图表的形式提供

## 思考链路
- 除非是专有技术名词，否则你应该在任何展示你思考链路或者结论的地方使用中文

## 调试流程
- 当你开始进行调试时，确保我明白你的调试思路后再进行具体的调试
