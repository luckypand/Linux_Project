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
class HeapTimer;

class EventLoop
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void loop();
    void quit();
    bool IsInloopthread();
    void RunInloop(Functor fc);
    void SengToPending(Functor fc);
    void WakeUp();
    void UpdateChannel(Channel* channel);
    void RemoveChannel(Channel* channel);

    void setTimer(HeapTimer* timer) { timer_ = timer; }

private:
    void handleRead();
    void DoPendingFunctors();

    bool looping_status_;
    bool quit_status_;
    const std::thread::id threadID_;

    std::unique_ptr<Poller> poller_;
    std::vector<Channel* > activeChannels_;

    int wakeupFd_;
    std::unique_ptr<Channel> wakeupChannel_;

    std::mutex mtx_;
    std::vector<Functor> pendingFunctors_;

    HeapTimer* timer_;
};
