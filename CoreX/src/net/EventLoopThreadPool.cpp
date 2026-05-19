#include "EventLoopThreadPool.hpp"
#include "EventLoopThread.hpp"
#include <memory>

/*
* @brief:
*     初始化线程池类(主线程)
*/
EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseloop,const std::string& name)
    :baseloop_(baseloop)
    ,name_(name)
{

}

/*
* @brief:
*     回收线程池
*/
EventLoopThreadPool::~EventLoopThreadPool()
{
    //EventLoopThread析构时自动调用join
    destroy = true;
}

/*
* @brief:
*     初始化线程
*/
void EventLoopThreadPool::start()
{
    if(started)
    {
        return;
    }
    started = true;

    for(int i = 0;i < numThreads_;i++)
    {
        std::string threadName = name_ + "-" + std::to_string(i);

        auto t = std::make_unique<EventLoopThread>(threadName);

        loops_.push_back(t->startLoop());        
        threads_.push_back(std::move(t));
    }
}

/*
* @brief:
*     核心架构函数：通过 Round-Robin 算法分发下一个子 Reactor
*/
EventLoop* EventLoopThreadPool::chooseNextLoop()
{
    EventLoop* loop = baseloop_;
    //loop池非空情况下，返回本次选择作为Recator的loop
    if(!loops_.empty())
    {
        loop = loops_[chooseThreadIndex];
        chooseThreadIndex = (chooseThreadIndex + 1) % loops_.size();
    }
    return loop;
}