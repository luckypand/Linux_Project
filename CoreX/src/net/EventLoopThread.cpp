#include "EventLoopThread.hpp"
#include "EventLoop.hpp"

/*
* @brief:
*     初始化线程
*/
EventLoopThread::EventLoopThread(std::string name)
    :name_(name)
{

}

/*
* @brief:
*     析构线程,等待线程销毁(主线程调用)
*/
EventLoopThread::~EventLoopThread()
{   
    exiting = true;
    //事件循环存在
    if(nullptr != loop_)
    {
        //退出循环后等待线程退出
        loop_->quit();
        if(thread_.joinable())
        {
            thread_.join();
        }
    }
}

/*
* @brief:
*     调用threadFunc()保证子线程EventLoop创建完成后返回记录其loop
*/
EventLoop* EventLoopThread::startLoop()
{
    //主线程创建线程，条件变量等待线程EventLoop创建完成后返回
    thread_ = std::thread(&EventLoopThread::threadFunc,this);
    EventLoop* loop = nullptr;
    {
        std::unique_lock<std::mutex> locker(mtx_);
        cond_.wait(locker,[this](){ return loop_ != nullptr; });
        loop = loop_;
    }
    return loop;
}

/*
* @brief:
*     创建EventLoop，子线程启动loop
*/
void EventLoopThread::threadFunc()
{
    //创建EventLoop，唤醒子线程wait，子线程进入loop
    EventLoop loopInstance;//子线程loop使得生存周期绑定
    {
        std::unique_lock<std::mutex> locker(mtx_);
        loop_ = &loopInstance;
    }
    //唤醒主线程获得子线程loop，子线程开始循环
    cond_.notify_one();
    loopInstance.loop();

    // 循环结束，清理指针
    std::unique_lock<std::mutex> locker(mtx_);    
    loop_ = nullptr;
}