#pragma once
#include "EventLoopThread.hpp"
#include <vector>
#include <string>

class EventLoop;

class EventLoopThreadPool
{
public:
    EventLoopThreadPool(EventLoop* baseloop,const std::string& name = "");
    ~EventLoopThreadPool();

    void start();                       //线程池开始初始化线程

    // 核心架构函数：通过 Round-Robin 算法分发下一个子 Reactor
    EventLoop* chooseNextLoop();  
    
    void setNumThreads(int numThreads = 0) { numThreads_ = numThreads; }     //设置线程池大小,默认单线程
    bool destroyTheadPool() { return destroy; }
private:
    EventLoop* baseloop_{nullptr}; //主线程loop
    std::string name_;
    bool destroy{false};           //线程池是否销毁 
    bool started{false};           //线程池是否创建      
    size_t numThreads_{0};            //线程数量
    size_t chooseThreadIndex{0};      //进行轮询时选择的线程池下标

    std::vector<std::unique_ptr<EventLoopThread>> threads_;  //记录子线程本身
    std::vector<EventLoop* > loops_;                         //记录子线程的loop       
};