#pragma once 
#include "Socket.hpp"
#include "Channel.hpp"
#include <functional>
#include "EventLoop.hpp"

class Eventloop;

class Acceptor
{
public:
    using newConnectionCallback = std::function<void(int sockfd)>;
    explicit Acceptor(EventLoop* loop,int port);
    ~Acceptor();

    void setNewConnectionCallback(newConnectionCallback& ncb) { newConnectionCallback_ = std::move(ncb); }   
    void listen();
private:
    void handleRead();

    EventLoop* loop_;
    Socket  acceptSocket_;
    Channel acceptChannel_;
    newConnectionCallback newConnectionCallback_;
};