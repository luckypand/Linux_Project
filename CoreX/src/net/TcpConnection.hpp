#pragma once
#include "EventLoop.hpp"
#include "Socket.hpp"
#include "Channel.hpp"
#include "Buffer.hpp"

class EventLoop;

class TcpConnection:public std::enable_shared_from_this<TcpConnection>
{
public:
    using connectionCallback = std::function<void(std::shared_ptr<TcpConnection>)>;
    using messageCallback = std::function<void(std::shared_ptr<TcpConnection>,Buffer&)>;
    using closeCallback = std::function<void(std::shared_ptr<TcpConnection>)>;

    TcpConnection(EventLoop* loop,int Socket);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; }
    bool connected() const { return state_ == kConnected; }

    void send(const std::string& message);
    void shutdown();

    void setConnectionCallback(connectionCallback& cb) { connectionCallback_ = std::move(cb); }
    void setMessageCallback(messageCallback& cb) { messageCallback_ = std::move(cb); }
    void setCloseCallback(closeCallback& cb) { closeCallback_ = std::move(cb); }

    // 被 TcpServer 调用，正式确立连接
    void connectEstablished();
    // 被 TcpServer 调用，正式销毁连接
    void connectDestroyed();

    private:
    enum StateE{kConnencting,kConnected,kDisconnecting,kDisconnected};

    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();
    void sendInLoop(const std::string& message);

    EventLoop* loop_;
    StateE state_;
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    Buffer inBuffer;
    Buffer outBuffer;

    connectionCallback connectionCallback_;
    messageCallback messageCallback_;
    closeCallback closeCallback_;
};