#include "Poller.hpp"
#include "Channel.hpp"
#include <unistd.h>
#include <string.h>

//红黑树下的fd状态机变化标记，记录的是fd本身与epoll挂载红黑树的状态
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
    int EventNum = ::epoll_wait(pollerfd_,&*events_.begin(),static_cast<int>(events_.size()),TimeoutMS);

    if(EventNum > 0)//事件发生数大于0，需要同步处理channel
    {
        for(int i = 0;i < EventNum;i++)
        {
            Channel* channel = static_cast<Channel*>(events_[i].data.ptr); //将对应channel取出
            channel->setretevent(events_[i].events);  //将实际发生的事情放入channel
            activeChannels_.push_back(channel);      //将活跃的channel放入activechannel
        } 
        //优化策略：动态扩容(监视到的事件发生数到达允许的最大值，扩容)
        if(EventNum == events_.size())
        {
            events_.resize(events_.size() * 2);
        }       
    }

}

/*
* @brief:
*     底层二叉树进行修改，本质是epoll_ctl的封装 
*/
void Poller::update(int operation,Channel* channel)
{
    struct epoll_event event;//设置将要被放入的事件
    memset(&event,0,sizeof(event));
    event.data.ptr = channel;
    event.events = channel->events();

    ::epoll_ctl(pollerfd_,operation,channel->Fd(),&event);
}

/*
* @brief:
*     利用状态机更新channel的状态,即fd与红黑树的状态
*/
void Poller::UpdateChannel(Channel* channel)
{
    const int index = channel->index();//记录此时的状态

    if(index == kNEW || index == KDELETED)//当该channel还没有加入或者被移出监视
    {
        if(index == kNEW)//fd新加入,此时map还没有映射，添加映射[fd,channel*]
        {
            channels_[channel->Fd()] = channel;
        }
        //若是删除重加入的fd，本身已经注册过，只需从更新状态和修改红黑树开始
        channel->setindex(KADDED);      //更新状态机
        update(EPOLL_CTL_ADD,channel);  //修改红黑树
    }
    else//本身状态已经被监视
    {
        if(channel->IsNoneEvent())//挂载后不关心该fd，删除
        {
            update(EPOLL_CTL_DEL,channel); 
            channel->setindex(KDELETED);
        }
        else//被监视了，并且还关注该fd
        {
            update(EPOLL_CTL_MOD,channel); 
        }
    }
}

/*
* @brief:
*     移除channel，即删除其在哈希表中的注册，如果本身在树上，则
    从epoll树上摘除，重新将状态置为kNEW
*/

void Poller::RemoveChannel(Channel* channel)
{
    int fd = channel->Fd();
    channels_.erase(fd);

    int index = channel->index();
    if(index == KADDED)
    {
        update(EPOLL_CTL_DEL,channel);
    }
    channel->setindex(kNEW);
}