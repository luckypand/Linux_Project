//记录待测试的内容
1. 测试场景 1（海量并发防泄漏）： 瞬间打入 10,000 个 TCP 连接，保持 10 分钟不断开，每秒发一个心跳小包。(√)

2. 测试场景 2（吞吐量压榨）： 开启 4 个子 Reactor 线程，使用 tcpkali 满载狂发 1KB 的数据包。

3. 编译时必须开启 -fsanitize=address (ASan)。压测结束正常退出时，ASan 报告 0 内存泄漏。(x)
## 原因解释：
    出现了内存泄露，ASAN显示出现在Tcpconncetion的析构上, 检测发现是shared_ptr在程序中形成环，导致无法正常析构
## 解决方法：
    将环断开，尝试使用weak_ptr接收Tcpconncetion的对象

# bug_solve
1.在回调中出现了内存泄露，ASAN显示出现在Tcpconncetion的析构上, 检测发现是shared_ptr在程序中形成环,即Tcpconnection中的closeCallback被设置了lamda储存了Tcpconnection的shared_ptr,导致无法正常析构

2.使用tcpkali（100连接，持续30秒）时发现连接全都被拒绝
# 结果：服务器拒绝连接，但是减少连接到10个维持30s时，顺利完成

3.tcpkali -c 10000 -T 10m -m "PING" --message-rate 1 127.0.0.1:8080进行测试时
# 结果：服务器保持10000连接的十分钟每秒一次的心跳包不断开

4.tcpkali -c 1000 -w 2 -T 1m -f 1kb_payload.bin 127.0.0.1:8080进行1000个连接开两个线程进行60s的狂发 1KB 的数据包过程，服务器断开，原因似乎同2
# 验证1：将tcpkali中执行tcpkali -c 10000 -T 60 -m "hello payload" --connect-rate=20000 127.0.0.1:9090发现服务器断开，但是取消消息发送，使用单纯的连接，发现连接本身没有断开，合理推测问题在socket的消息接发上，可能与buffer相关
# 结果：通过引入高低水位机制控制channel对读事件的使能来控制Outbuffer的容量，使得发送消息速度小于生产消息速度时内存不会被撑爆

5.Asan报告显示出现direct leak,并且经过排查，似乎是因为Poller管理的unordered_map内的结点没有被释放
## 原因 : 子线程本来要绑定对应subloop的循环，但是代码绑定为主线程的loop，导致子线程的Tcpconncetion被主线程持有。但是connectEstablished是在子线程下执行的，造成本应该在子线程同时完成连接和释放，变成了子线程连接，主线程释放，不同线程之间数据竞争，使得unordered_map的结点发生丢失，成为孤儿结点，造成内存泄漏
**错误代码 ：TcpConnectionPtr conn = std::make_shared<TcpConnection>(loop_, sockfd)

6.在进行RpcServer的Benchmark 模式时出现内存错误
## 原因 :FindServiceByName使用的是短名查找而非package的全名查找

7.启动内嵌 RPC 服务器后一会后退出并提示terminate called recursively与Aborted
## 原因 :connectionCallback_为空，导致执行时抛出异常，由于异常没有被捕获，一路抛至thread的入口导致触发了std::terminate()
- ps：客户端主动断开不触发该bug，服务端主动断开触发该bug，原因是客户端主动断开时状态机会自动调整，因此没有触发空的connectionCallback_

8.进行RTT 延迟测试时发起 10 万次连续的 RPC 同步调用，发现平均的回包延迟总是控制在几十微秒左右的大小，但是最后却总会卡在10s左右的总回包时长
## 原因 :wakefd写入时被误设为0，导致被放入pending队列中的连接任务没有唤醒对应的sub Reactor poller，等到10s阻塞结束后才一次性处理所有的连接任务和处理对应的数据和任务

9.修改了wakefd被写入的值后，发现虽然没有10s的延迟了，但是到发送后期会出现几十到一百次左右的几千us左右的尾延迟,P99在大约200us。
## 原因 ：通过在proto中加入时间戳后首先发现，超过500us的请求绝大多数耗时在    上行网络 (client→server):     838.2 us  (84.8%)
- 1.在回包时设置Tcpconnection的socket为NoDelay，减少Nagle的等包,最终使得P99降低至80us左右,控制长尾延迟在可控范围内
    Tail latency amplification:
    P99  / P50:       1.9x  ✓ 正常
    P999 / P50:       5.8x  ✓ 正常

10.在使用日志系统进行异步打时间戳时，发现日志出现递归加锁造成的死锁现象
## 原因 ：使用某些函数时已经取得锁了，但是在其中调用了一些例如empty或者size等函数判断又进行了加锁。

11.使用异步日志写入BlockQueue时，发现造成了死锁
## 原因 ：出现了ABBA的循环死锁情况，并且还不能通过简单的固定获取锁的顺序解决，因为在异步推出deque中的数据后还需要获得log中的文件指针，所以还需要获得log的大锁，因此需要将两者的锁永不重叠，即使用后马上释放锁，而不是等待其走出生命周期

12.使用perf测试RPC部分时，发现在findService以及findMethod的部分耗时长
## 解决方案：将这两部分使用unorder_map进行映射的方式，减少了查找开销

13.在 ROS noetic 环境下执行 ./build.sh 编译 ROS Bridge 模块时，出现 6 个编译错误导致构建失败
## 涉及文件：`ShmImageTransporter.cpp` / `TopicBridge.cpp` / `ServiceBridge.cpp` / `RosBridgeEngine.cpp`
## 根本原因：ROS noetic 的 `ros::SerializedMessage` 缺少标准 traits 成员，且部分 API 与代码使用的接口不兼容
## 新增文件：`src/ros_bridge/RosSerializedMessageTraits.hpp`（公共 traits 特化头文件）

---- 分项说明 ----

① ShmImageTransporter.cpp — ShmMemoryPool 接口不匹配（2 处）
  - 错误 1：构造函数传入 `bool isCreator`，但声明为 `ShmMemoryPool::Mode mode`
    > 报错：`no known conversion for argument 3 from 'bool' to 'ShmMemoryPool::Mode'`
    > 修复：`isCreator ? ShmMemoryPool::CREATE : ShmMemoryPool::ATTACH`
  - 错误 2：调用了 `pool_->data()`，但实际方法名为 `GetMappedptr()`
    > 报错：`'class ShmMemoryPool' has no member named 'data'`
    > 修复：`pool_->data()` → `pool_->GetMappedptr()`

② TopicBridge.cpp — ros::message_traits::md5sum 推导失败
  - 报错：`'__s_getMD5Sum' is not a member of 'ros::SerializedMessage'`
  - 原因：SerializedMessage 缺少 `__s_getMD5Sum` 静态方法，模板无法实例化
  - 修复：SubscribeOptions / AdvertiseOptions 的 md5sum 字段直接使用 ROS 通配符 `"*"`

③ ServiceBridge.cpp — ServiceClient::call() 超时参数类型错误
  - 报错：`no matching function for call to 'ros::ServiceClient::call(..., ros::Duration&)'`
  - 原因：ServiceClient 的 3 参数重载第三参数是 `const std::string& md5sum`，非 Duration
  - 修复：改为两参数调用 `client_.call(srvReq, srvResp)`，超时交由上层 RPC 框架处理

④ RosBridgeEngine.cpp — 前向声明导致类型不完整
  - 报错：`DynamicServiceAdapter*` 无法转为 `RpcServiceAdapter*`
  - 原因：仅前向声明 `class DynamicServiceAdapter`，编译器不知其继承自 RpcServiceAdapter
  - 修复：添加 `#include "DynamicServiceAdapter.hpp"`

⑤ TopicBridge.cpp + ServiceBridge.cpp — SerializedMessage 缺少 ROS traits（核心问题）
  - 涉及的模板调用链：
    · `Publisher::publish(SerializedMessage)`     → 需要 message_traits::MD5Sum/DataType + Serializer
    · Subscriber 反序列化路径                     → 需要 Serializer::read
    · `ServiceClient::call(SerializedMessage, …)` → 需要 service_traits::MD5Sum/DataType
  - 原因：ROS noetic 的 SerializedMessage 是轻量级容器，未像普通消息类型一样由代码生成器生成 traits
  - 修复：新建 `RosSerializedMessageTraits.hpp`，统一补全三类模板特化：
    · `message_traits::MD5Sum`      → 返回 `"*"`
    · `message_traits::DataType`    → 返回 `"*"`
    · `message_traits::Definition`  → 返回 `""`
    · `service_traits::MD5Sum`      → 返回 `"*"`
    · `service_traits::DataType`    → 返回 `"*"`
    · `serialization::Serializer`   → write 直接 memcpy / read 从流重建 / serializedLength 返回 num_bytes
    TopicBridge.cpp 与 ServiceBridge.cpp 均引入该头文件

-------------
# programme_improve
1.Output Buffer 无限膨胀导致 OOM (内存溢出),没有进行内存水位管理，所以
导致当疯狂收包时buffer被撑爆,可以引入高水位回调 (High Water Mark Callback) 和背压 (Backpressure) 机制。

2.将代码中涉及到回调的变量，例如shared_ptr<T>的参数修改为shared_ptr<T>&,这样可以减少原子操作,对于指针传入的情况同理。

3.后续切换成两台服务器之间互传数据时再进行benchmark查看是否存在某些单次几ms的请求存在

4.优化RPC中的RpcServiceAdapter模块，目前的实现路径是

