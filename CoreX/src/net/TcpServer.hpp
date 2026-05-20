#pragma once
#include "EventLoopThreadPool.hpp"
#include "EventLoopThread.hpp"
#include "EventLoop.hpp"
#include "Acceptor.hpp"
#include "TcpConnection.hpp"
#include <memory>
#include <atomic>
#include <unordered_map>

class TcpServer
{
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

    TcpServer(EventLoop* loop,const std::string& inport,uint16_t port,const std::string& name);
    ~TcpServer();
    
    void setThreadNum(int numThreads);
    void newConnection(int sockfd, const std::string& peerAddr);
    void start();

    //开放给用户的回调接口
    void setConnectionCallback(TcpConnection::connectionCallback cb) { connectionCallback_ = std::move(cb); }
    void setMessageCallback(TcpConnection::messageCallback cb) { messageCallback_ = std::move(cb); }
    void setWriteCompleteCallback(TcpConnection::writeCompleteCallback cb) { writeCompleteCallback_ = std::move(cb); }
private:
    void removeConnection(const TcpConnectionPtr& conn,const std::string& connectName);
    void removeConnectionInLoop(const TcpConnectionPtr& conn,const std::string& connectName);

    EventLoop* loop_{nullptr};  //main Reactor

    const std::string inport_;  //服务器自身监听ip
    const std::string name_;    //服务器名字

    //TcpServer的接触资源(注意，线程池生命周期不完全绑定TcpServer)
    std::unique_ptr<Acceptor> acceptor_;
    std::shared_ptr<EventLoopThreadPool> threadPool_;

    //业务回调(提供给用户层面的)
    TcpConnection::connectionCallback connectionCallback_;
    TcpConnection::messageCallback messageCallback_;
    TcpConnection::closeCallback closeCallback_;
    TcpConnection::writeCompleteCallback writeCompleteCallback_;

    //状态位记录
    std::atomic_int started{0};//记录第一次启动服务器
    int nextConnectId{1};      //记录下一个要轮询的Reactor

    //记录表，记录对应的线程和连接
    std::unordered_map<std::string,TcpConnectionPtr> connections_;
};