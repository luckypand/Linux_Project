#include "Channel.hpp"
#include "EventLoop.hpp"
#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop,int fd_)
 :my_loop_(loop)
 ,fd_(fd_)
 ,events_(0)
 ,retevent_(0)
 ,index_(-1)
{

}

Channel::~Channel()
{

}

void Channel::Event_handle()
{
    if((retevent_ & EPOLLERR) && ErrorCallback_)
    {
        ErrorCallback_();
    }
    if(retevent_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP) && ReadCallback_)
    {
        ReadCallback_();
    }
    if((retevent_ & EPOLLOUT) && WriteCallback_)
    {
        WriteCallback_();
    }
}

void Channel::update()
{
    my_loop_->UpdateChannel(this);
}