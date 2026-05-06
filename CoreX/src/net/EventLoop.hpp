#pragma once
#include <iostream>
#include <mutex>
#include <vector>
#include <memory>
#include <thread>
#include <functional>
#include <sys/types.h>

class Poller;
class Channel;

class EventLoop
{
public:
    using Functor = std::function<void()>;  //定义任务类型

    EventLoop();
    ~EventLoop();

    void loop();            //开始进行loop事件处理
    void quit();            //退出该事件循环
    bool IsInloopthread();  //判断调用线程是否为loop线程
    void RunInloop(Functor fc);       //依据线程进行工作
    void SengToPending(Functor fc);   //将待办任务放入pending队列
    void WakeUp();                    //外部任务函数放入队列时提醒
    void UpdateChannel(Channel* channel);
    void RemoveChannel(Channel* channel);
private:
    void handleRead();          // wakeupFd_的读回调
    void DoPendingFunctors();   //执行在pending队列中待办的具体事项

    // 1. 状态与线程归属
    bool looping_status_;
    bool quit_status_;
    const std::thread::id threadID_;// 记录这个Loop是被哪个线程创建的（核心属性！）

    // 2. 核心组件（对 epoll 的封装）
    std::unique_ptr<Poller> poller_;
    std::vector<Channel* > activeChannels_;

    // 3. 跨线程唤醒
    int wakeupFd_;
    std::unique_ptr<Channel> wakeupChannel_;

    // 4. 任务队列与锁
    std::mutex mtx_;
    std::vector<Functor> pendingFunctors_;// 存放其他线程扔过来的任务
};