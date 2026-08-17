# Claude Code Implementation Specification

# CoreX Shared Memory Pub/Sub Transport

## 1. Implementation Objective

请基于当前 CoreX 工程实现高性能共享内存 Pub/Sub Transport 模块。

设计目标：

* 支持机器人同机节点之间的大数据共享；
* 支持 Camera、LiDAR、PointCloud 等高频数据；
* 避免多个消费者重复 memcpy；
* 支持多生产者、多消费者；
* 支持动态订阅；
* 支持 STREAM / KEEP_LAST / LATCH 数据生命周期。

系统边界：

```
ROS Sensor Node

        |
        | TCPROS

        v

CoreX ROS Bridge

        |
        | copy once

        v

Shared Memory Transport

        |
        |
 ------------------
 |        |        |
Node1   Node2   Node3

```

注意：

不实现 ROS1 Driver 端零拷贝。

优化范围：

CoreX Bridge 到 CoreX Internal Node。

---

# 2. Required Architecture

整体结构：

```
                TopicManager


                     |

        TopicChannel Map


                     |

 ------------------------------------------------

 |                     |                        |

Camera Topic        Lidar Topic            IMU Topic


 |                     |                        |

RingBuffer          RingBuffer             RingBuffer


 |                     |                        |

Block ID            Block ID               Block ID


                     |

              Shared BufferPool


                     |

                  Payload

```

---

# 3. Module Design

必须实现：

```
ipc/

├── shm_block.h

├── buffer_pool.h

├── topic_channel.h

├── topic_manager.h

├── subscriber_registry.h

├── subscriber_queue.h

├── ring_buffer.h

├── qos_policy.h

├── history_cache.h

└── tests/

```

---

# 4. Buffer Pool

BufferPool负责：

* 管理共享内存Block；
* 分配数据块；
* 回收数据块。

Block结构：

```cpp
struct ShmBlock
{
    BlockHeader header;

    uint8_t payload[];

};

```

Header：

```cpp
struct BlockHeader
{

atomic<State> state;

atomic<uint32_t> ref_count;


uint32_t block_id;

uint32_t topic_id;

uint64_t timestamp;

uint32_t size;

QoSType qos;


};

```

状态：

```
FREE

WRITING

READY

```

状态转换：

```
FREE

 |

CAS

 |

WRITING

 |

write payload

 |

READY

 |

ref_count==0

 |

FREE

```

---

# 5. Topic Channel

每一个topic维护独立Channel。

例如：

```
/camera/image_raw


/lidar/cloud


/imu/data

```

对应：

```
CameraChannel

LidarChannel

IMUChannel

```

Channel包含：

```cpp
class TopicChannel
{

RingBuffer<uint32_t> ring;

SubscriberRegistry subscribers;

HistoryCache history;

QoS qos;


};

```

---

# 6. RingBuffer Design

RingBuffer只保存：

```
Block ID

```

例如：

```
Camera RingBuffer


[100]

[101]

[102]

```

禁止保存：

* PointCloud
* Image
* Payload

---

## Atomic Requirement

atomic只用于：

```cpp
head

tail

block state

ref_count

```

数据：

```cpp
uint32_t buffer[]

```

无需atomic。

---

# 7. Subscriber Registry

维护当前订阅者。

结构：

```cpp
struct SubscriberInfo
{

uint32_t id;


Queue<uint32_t> queue;


uint64_t heartbeat;


State state;


};

```

Topic：

维护：

```
subscriber list

```

---

# 8. Publish Workflow

Producer产生数据：

流程：

```
1.

BufferPool申请Block


2.

FREE -> WRITING


3.

写payload


4.

WRITING -> READY


5.

查询SubscriberRegistry


6.

设置ref_count


7.

复制Block ID到所有Subscriber Queue

```

例如：

当前：

```
Subscriber:

SLAM

AI

Logger

```

Block:

```
ref_count=3

```

Queue:

```
SLAM Queue

100


AI Queue

100


Logger Queue

100

```

---

# 9. Subscriber Workflow

消费者：

```
Queue.pop()

        |

获得Block ID

        |

BufferPool.get(id)

        |

读取payload

        |

release()

```

release：

```
ref_count--

```

当：

```
ref_count==0

```

释放Block。

---

# 10. Subscriber异常退出处理

必须实现：

Subscriber Heartbeat。

SubscriberInfo:

```cpp
heartbeat_timestamp

```

TopicManager周期检查：

```
timeout

```

如果：

```
Subscriber timeout

```

执行：

1. 删除Subscriber；

2. 清理Queue；

3. 回收该Subscriber持有Block。

避免：

```
Block永久READY

```

---

# 11. QoS Policy

实现：

```cpp
enum QoS

{

STREAM,

KEEP_LAST,

LATCH

};

```

---

# 12. STREAM

应用：

* Camera
* LiDAR
* IMU

规则：

新订阅者：

只能收到未来数据。

测试：

```
Producer产生100帧


Subscriber A提前加入


Subscriber B中途加入


验证：

B不能收到历史数据

```

---

# 13. KEEP_LAST

保存最近N个Block。

例如：

```
depth=5


95

96

97

98

99

```

测试：

```
Producer产生100帧


新Subscriber加入


获取最后5帧

```

---

# 14. LATCH

保存最新Block。

应用：

* Map
* Config

测试：

```
Producer发布一次


停止


Subscriber随后加入


立即收到最新数据

```

---

# 15. Concurrency Tests

必须实现：

## SPSC

单生产者单消费者。

验证：

* 顺序
* 无丢失

---

## MPSC

多个Producer：

```
Camera

Lidar

IMU

```

一个Consumer。

---

## SPMC

一个Producer：

多个Consumer。

验证：

同一个Block：

多个消费者均能读取。

---

## MPMC

4 Producer

4 Consumer

验证：

* 无死锁；
* 无数据覆盖；
* 无Block泄漏。

---

# 16. Performance Benchmark

测试：

## memcpy vs Shared Memory

指标：

* latency
* throughput
* CPU usage

模拟：

Camera:

```
6MB/frame

30Hz

```

LiDAR:

```
5MB/frame

10Hz

```

测试：

1消费者；

5消费者。

---

# 17. Cache Line Optimization

以下结构：

需要：

```cpp
alignas(64)

```

对象：

```
head

tail

state

ref_count

```

测试：

比较：

* aligned
* unaligned

---

# 18. Deliverables

最终输出：

代码：

```
shared_memory/

```

测试：

```
test_spsc

test_mpsc

test_spmc

test_mpmc

test_stream

test_keep_last

test_latch

test_subscriber_timeout

test_benchmark

```

文档：

包含：

* 架构说明；
* 生命周期；
* 并发模型；
* 测试结果。

---

# 19. Implementation Order

严格按照：

1. ShmBlock

2. BufferPool

3. TopicChannel

4. RingBuffer

5. SubscriberRegistry

6. SubscriberQueue

7. Reference Counting

8. Heartbeat Recovery

9. QoS

10. Tests

11. Benchmark

禁止跳过测试直接实现业务接口。
