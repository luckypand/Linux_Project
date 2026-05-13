#pragma once

#include <chrono>
#include <functional>
#include <vector>
#include <unordered_map>

// 使用现代 C++ 的高精度时钟
using TimeoutCallback = std::function<void()>;
using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::milliseconds;
using TimeStamp = Clock::time_point;

//存入时间堆的时间节点
struct TimeNode
{
    int fd_;                //对应的fd
    TimeStamp expire_;      //失效时间，用来作为堆排
    TimeoutCallback tcb;    //对应到点执行命令

    bool operator<(const TimeNode& t) const
    {
        return expire_ < t.expire_;
    }

    bool operator>(const TimeNode& t) const
    {
        return expire_ > t.expire_;
    }
};

class HeapTimer
{
public:
    HeapTimer();
    ~HeapTimer();

    void adjust(int fd, int newExpiresMs);            //调整指定fd的定时器的expire_属性
    void add(int fd,int TimeoutMs,TimeoutCallback tcb);   //添加新的时间点
    void top_pop();                                       //弹出堆顶
    void clear();                                     //调用TimeHeap_和ref_的清理流程
    void doWork(int fd);                              //主动触发并删除定时器 (例如客户端主动断开)
    void tick();                                      // 检查并清理所有已超时的定时器 (EventLoop 的心跳函数)
    int  Getclosetick();                              // 获取距离下一个定时器超时的剩余时间 (用于 epoll_wait)
private:
    void swapnode(size_t i,size_t j);                 //交换结点 
    void del_(size_t t);                              //指定时间点进行删除
    void siftup(size_t i);                            //向上调整默认到堆顶，故只需要传入需调整的结点
    bool siftdown(size_t i,size_t n);                 //向下调整需要调整到尾，故除了调整结点还需要传入堆大小

    std::vector<TimeNode> TimeHeap_;                  //时间堆
    std::unordered_map<int,size_t> ref_ ;             //搜索表 [fd,下标]
};
