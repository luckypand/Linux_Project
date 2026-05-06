CoreX/
├── CMakeLists.txt              # 顶层 CMake 构建脚本（指挥全局）
├── README.md                   # 项目说明（秋招写在 GitHub 首页的门面）
├── build.sh                    # 一键编译脚本（显得专业）
│
├── proto/                      # 【协议定义区】
│   ├── message.proto           # 业务数据结构（如传感器数据、点云数据）
│   └── rpc_meta.proto          # RPC 元数据格式（定义 Header、请求ID等）
│
├── src/                        # 【核心源码区 - 你的代码主力库】
│   ├── net/                    # 模块一：Reactor 网络引擎 (抄 Muduo)
│   │   ├── EventLoop.h/.cc
│   │   ├── Channel.h/.cc
│   │   ├── EpollPoller.h/.cc
│   │   ├── TcpServer.h/.cc
│   │   ├── TcpConnection.h/.cc
│   │   └── Buffer.h/.cc        # 解决粘包的核心数据结构
│   │
│   ├── ipc/                    # 模块二：车载共享内存引擎 (抄 Apollo)
│   │   ├── ShmSegment.h/.cc    # 封装 mmap / shm_open
│   │   ├── RingBuffer.h/.cc    # 无锁环形队列 (std::atomic)
│   │   └── ConditionNotifier.h/.cc # 进程间状态通知机制
│   │
│   ├── rpc/                    # 模块三：RPC 框架层
│   │   ├── RpcServer.h/.cc     # 接收 RPC 请求
│   │   ├── RpcChannel.h/.cc    # 继承 google::protobuf::RpcChannel
│   │   └── RpcCodec.h/.cc      # 负责把 Protobuf 序列化并加报文头
│   │
│   └── websocket/              # 模块四：你的 Tinywebsocket (已有的资产)
│       ├── WebSocketServer.h/.cc
│       └── HttpParser.h/.cc    # 握手解析
│
├── apps/                       # 【业务打包区 - “一鱼两吃”的体现】
│   ├── gateway/                # 包装为《边缘网关》
│   │   └── gateway_main.cc     # main函数：组装 net 模块和 websocket 模块
│   │
│   └── auto_middleware/        # 包装为《车载中间件》
│       ├── publisher_main.cc   # main函数：模拟感知节点（写共享内存）
│       └── subscriber_main.cc  # main函数：模拟规划节点（读共享内存）
│
├── tests/                      # 【单元测试区】(用 GTest)
│   ├── test_buffer.cc          # 测试网络收发缓冲区
│   ├── test_ringbuffer.cc      # 测试无锁队列的并发安全性
│   └── test_rpc_codec.cc       # 测试粘包/半包解析逻辑
│
└── third_party/                # 【第三方依赖库】(如果不用 apt 安装)
    └── moodycamel/             # 极速无锁队列源码 (可选)