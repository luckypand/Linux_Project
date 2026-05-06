#include "Poller.hpp"
#include <unistd.h>

//红黑树下的fd状态机变化标记
const int kNEW = -1;    // 新的，还不在 epoll 红黑树上
const int KADDED = 1;   // 已经在树上
const int KDELETED = 2; // 已经从树上摘除，但对象还在

Poller::Poller(EventLoop* loop)
    :my_EventLoop_(loop)
    ,pollerfd_(::epoll_create1(EPOLL_CLOEXEC))
    ,events_(events_size_)
{

}

Poller::~Poller()
{
    ::close(pollerfd_);
}

void Poller::poll(int TimeoutMS,ChannelList& activeChannels_)
{
    //统计发生事件的fd
}