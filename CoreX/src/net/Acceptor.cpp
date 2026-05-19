#include "Acceptor.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>

/*
* @brief:
*     创建监听Socket
*/
Acceptor::Acceptor(EventLoop* loop,int port)
    :loop_(loop)
    ,acceptSocket_(
        ::socket(AF_INET,
        (SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC),
        0)
    )
    ,acceptChannel_(loop,this->acceptSocket_.fd())
{
    acceptSocket_.setReuseAddr(true); //可以快速重启服务器
    acceptSocket_.bindAddress(port);  //绑定端口

    // 当有新连接来时，epoll 会通知 acceptChannel_，从而触发 handleRead
    //acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead,this));
    acceptChannel_.setReadCallback([this](){this->handleRead();});
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableall();
    loop_->RemoveChannel(&acceptChannel_);
}

/*
* @brief:
*     监听连接的Socket
*/
void Acceptor::listen()
{
    acceptSocket_.listen();
    acceptChannel_.enableReading();
}

/*
* @brief:
*     监听的Socket传入的读回调处理
*/
void Acceptor::handleRead()
{
    //储存连接的Scoket对应的网路信息
    struct sockaddr_in acceptAddr;

    int connfd = acceptSocket_.accept(&acceptAddr);
    if(connfd > 0)
    {
        if(newConnectionCallback_)
        {
            //将对端地址转换为 "ip:port" 字符串
            char peerIP[64] = {0};
            ::inet_ntop(AF_INET, &acceptAddr.sin_addr, peerIP, sizeof(peerIP));
            std::string peerAddr = std::string(peerIP) + ":" + std::to_string(::ntohs(acceptAddr.sin_port));
            newConnectionCallback_(connfd, peerAddr);
        }
        else
        {
            ::close(connfd);
        }
    }
}