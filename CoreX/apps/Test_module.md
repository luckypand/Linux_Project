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

9.修改了wakefd被写入的值后，发现虽然没有10s的延迟了，但是到发送后期会出现几十到一百次左右的几千us左右的尾延迟,
## 原因 ：通过在proto中加入时间戳后首先发现，超过500us的请求绝大多数耗时在    上行网络 (client→server):     838.2 us  (84.8%)

10.在使用日志系统进行异步打时间戳时，发现日志出现递归加锁造成的死锁现象
## 原因 ：使用某些函数时已经取得锁了，但是在其中调用了一些例如empty或者size等函数判断又进行了加锁。

11.使用异步日志写入

-------------
# programme_improve
1.Output Buffer 无限膨胀导致 OOM (内存溢出),没有进行内存水位管理，所以
导致当疯狂收包时buffer被撑爆,可以引入高水位回调 (High Water Mark Callback) 和背压 (Backpressure) 机制。

2.将代码中涉及到回调的变量，例如shared_ptr<T>的参数修改为shared_ptr<T>&,这样可以减少原子操作,对于指针传入的情况同理。



