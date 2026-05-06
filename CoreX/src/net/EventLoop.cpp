#include "EventLoop.hpp"
#include "Poller.hpp"
#include "Channel.hpp"
#include <sys/eventfd.h>
#include <cstdlib>
#include <unistd.h>
#include <assert.h>
#include <mutex>

namespace
{
    int createEventfd()
    {
        int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (evtfd < 0) 
        {
            std::cerr << "Failed in eventfd" << std::endl;
            abort();
        }
        return evtfd;        
    }

}

/*
* @brief:
*     构造函数，为唤醒loop的
*/
EventLoop::EventLoop()
    :looping_status_(false)
    ,quit_status_(false)
    ,threadID_(std::this_thread::get_id())
    ,poller_(std::make_unique<Poller>(this)) //传入this因为poller_内部需要调用EventLoop的方法
    ,activeChannels_()
    ,wakeupFd_(createEventfd())
    ,wakeupChannel_(std::make_unique<Channel>(this,wakeupFd_))
    ,mtx_()
    ,pendingFunctors_()
{
    //使用lamda传入读回调参数
    wakeupChannel_->setReadCallback(
        [this](){this->handleRead();}
    );        //唤醒队列绑定对应的读写事件 
    wakeupChannel_->enableReading();
}

/*
* @brief:
*      释放Eventloop管理的资源
*/
EventLoop::~EventLoop()
{
    wakeupChannel_->disableall();//标记channel内部事件消息
    ::close(wakeupFd_);//释放wakefd
}

/*
* @brief:
*     核心的循环执行机制，在这里处理每个channel的任务并执行pending任务
*/
void EventLoop::loop()
{
    assert(!looping_status_);//防止其他的线程在本loop启动后反复启动该loop
    assert(IsInloopthread());
    looping_status_ = true;
    quit_status_ = false;

    while(!quit_status_)//loop持续执行
    {
        activeChannels_.clear(); //先清空已处理的channel

        poller_->poll(10000,activeChannels_);         //阻塞等待epoll监控的fd发生新事件

        for(auto& channel_ptr : activeChannels_)
        {
            channel_ptr->Event_handle(); //处理每个channel对应的事件
        }
        DoPendingFunctors();
    }

    looping_status_ = false; //执行到这说明loop已经quit了
}

/*
* @brief:
*     设置当外部线程触发fd时进行读操作,即解除外部新消息提示(LT模式需读取，否则不断提醒)
*/
void EventLoop::handleRead()
{
    uint64_t alarm = 0;
    ::read(wakeupFd_,&alarm,sizeof(alarm));
}

/*
* @brief:
*     处理pending下的待办任务
*/
void EventLoop::DoPendingFunctors()
{
    std::vector<Functor> swap_pending;
    {
        std::lock_guard<std::mutex> locker(mtx_); //加锁swap
        swap(swap_pending,pendingFunctors_);
    }

    for(auto& func : swap_pending)
    {
        func();
    }
}

/*
* @brief:
*     //判断调用线程是否为loop线程s
*/
bool EventLoop::IsInloopthread()
{
    return threadID_ == std::this_thread::get_id();
}

/*
* @brief:
*     //依据线程进行工作
*/
void EventLoop::RunInloop(Functor fc)
{
    if(IsInloopthread())
    {
        fc();
    }
    else
    {
        SengToPending(fc);
    }
}

/*
* @brief:
*     以eventfd的形式唤醒主线程loop
*/
void EventLoop::WakeUp()
{
    uint64_t alarm = 0;
    ::write(wakeupFd_,&alarm,sizeof(alarm));
}

/*
* @brief:
*     //将待办任务放入pending队列,使用右值传递模式减少拷贝
*/
void EventLoop::SengToPending(Functor fc)
{
    //加锁投递任务
    {
        std::lock_guard<std::mutex> locker(mtx_);
        pendingFunctors_.push_back(std::move(fc));
    }   
    
    //非本线程投递任务，需要唤醒以fd形式告知主线程loop
    if(!IsInloopthread())
    {
        WakeUp();
    }
}

/*
* @brief:
*     对Channel的开放接口，使得其能更新epoll
*/
void EventLoop::UpdateChannel(Channel* channel)
{
    poller_->UpdateChannel(channel);
}

/*
* @brief:
*     对Channel的开放接口，使得其能删除epoll
*/
void EventLoop::RemoveChannel(Channel* channel)
{
    poller_->RemoveChannel(channel);
}