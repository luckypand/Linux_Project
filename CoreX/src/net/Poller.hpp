#pragma once
#include <vector>
#include <unordered_map>
#include <sys/epoll.h>

class Channel;
class EventLoop;

class Poller
{
public:
    using ChannelList = std::vector<Channel* >;

    Poller(EventLoop* loop);
    ~Poller();

    // 核心：阻塞等待内核事件，将活跃的 Channel 填入 activeChannels
    void poll(int TimeoutMS,ChannelList& activeChannels_);

    // 维护红黑树：增、删、改    
    void UpdateChannel();
    void RemoveChannel();
private:
    void update(int TimeoutMs,Channel* channel);          // 维护红黑树实际调用的函数，封装了epoll_ctl

    const static int events_size_ = 16;
    EventLoop* my_EventLoop_;
    int pollerfd_;
    std::vector<struct epoll_event> events_;     // 存放 epoll_wait 返回的事件组
    std::unordered_map<int, Channel*> channels_; // fd 到 Channel* 的映射 (哈希表提速)
};