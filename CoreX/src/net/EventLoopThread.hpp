#pragma once 
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>

class EventLoop;

class EventLoopThread
{
public:
    EventLoopThread(std::string name);
    ~EventLoopThread();

    EventLoop* startLoop();     //开启loop事件
    void threadFunc();          //子线程绑定函数
private:

    EventLoop* loop_{nullptr};
    bool exiting{false};
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cond_;
    std::string name_;
};