#pragma once
#include <functional>
#include <memory>

class EventLoop;
class Poller;

class Channel
{
public:
    using EventCallback_ = std::function<void()>;
    Channel(EventLoop* loop,int fd_);  
    ~Channel();

    // 设置回调函数 (使用 std::move 避免拷贝)   
    void setReadCallback(EventCallback_ rck)   { ReadCallback_ = std::move(rck); }
    void setWriteCallback(EventCallback_ wck) { WriteCallback_ = std::move(wck); }
    void setErrorCallback(EventCallback_ eck) { ErrorCallback_ = std::move(eck); }

    // 状态修改：通知 EventLoop 去更新 epoll 里的监听事件
    void enableReading() { events_ |= kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableall() { events_ |= kNoneEvent; }
    void setindex(int idx_) { index_ = idx_; }
    void setretevent(uint32_t  revt) { retevent_ = revt; }

    // 核心：由 EventLoop 调用，根据 revents_ 执行对应的回调    
    void Event_handle();

    // 状态查询    
    int Fd() const { return fd_; }
    int index() const { return index_; }
    bool IsNoneEvent() { return events_ == kNoneEvent; } //判断用户是否不再关心该fd的数据
    uint32_t events() { return events_; }
    EventLoop* myEventLoop() const { return my_loop_; }
    
private:
    void update();  //调用更新channel的状态

    //用于设置Channel中对底层 fd 关心哪些 IO 事件的标志位
    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* my_loop_;
    const int fd_;   //对应管理的fd
    uint32_t events_;     //用户关心的事件
    uint32_t retevent_;   //实际发生的事件
    int index_;      //表示Channel自身在epoll中的状态

    EventCallback_ ReadCallback_;
    EventCallback_ WriteCallback_;
    EventCallback_ ErrorCallback_;
}; 