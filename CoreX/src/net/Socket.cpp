#include "Socket.hpp"
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>   
#include <cstring>   
#include <iostream>
#include <netinet/tcp.h>

/*
* @brief:
*     移动赋值
*/
Socket& Socket::operator=(Socket&& rhs) noexcept
{
    //右值引用赋值先排除自身赋值
    if(this != &rhs)
    {
        //先关闭可能已经打开的socket
        //socket覆盖后将右值socket进行值销毁
        if(sockfd_ >= 0)
        {
            ::close(sockfd_);
        }
        sockfd_ = rhs.sockfd_;
        rhs.sockfd_ = -1;
    }
    return *this;
}

/*
* @brief:
*     封装bind，绑定Socket到对应端口
*/
void Socket::bindAddress(int port) //主机小端字节序
{
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htonl(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY); //监听本机所有可用网卡

    //进行bind，失败直接警告后退出
    int ret = ::bind(sockfd_,(struct sockaddr*)&addr,sizeof(addr));
    if(ret < 0)
    {
        std::cerr << "Socket::bindAddress fail,port: " << port << " errno:" << errno << std::endl;
        abort();
    }
}

/*
* @brief:
*     封装listen，监听Socket中的全连接请求
*/
void Socket::listen()
{
    int ret = ::listen(sockfd_,SOMAXCONN);

    if(ret < 0)
    {
        std::cerr << "Socket::listen failed\n";
        abort();        
    }
}

/*
* @brief:
*     封装accept，接受Socket中的全连接请求
* @parameter:
      peeraddr:传出参数，存储客户端的真实IP和端口
*/
int Socket::accept(struct sockaddr_in* peeraddr)
{
    socklen_t addrlen = sizeof(struct sockaddr_in);

    int connfd = ::accept4(sockfd_,(struct sockaddr*)peeraddr,&addrlen,SOCK_CLOEXEC | SOCK_NONBLOCK);
    if(connfd < 0)
    {
        int savedErrno = errno;

        if(savedErrno != EAGAIN && savedErrno != EWOULDBLOCK)
        {
            std::cerr << "Socket::accept failed, errno: " << savedErrno << "\n";
        }
    }
    return connfd; // 无论成功与否直接返回，由上层 Acceptor 判断
}

/*
* @brief:
*     以下四个函数是对socket的优化调参
*/
void Socket::setReuseAddr(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, static_cast<socklen_t>(sizeof optval));
}

void Socket::setReusePort(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, static_cast<socklen_t>(sizeof optval));
}

void Socket::setTcpNoDelay(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, static_cast<socklen_t>(sizeof optval));
}

void Socket::setKeepAlive(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, static_cast<socklen_t>(sizeof optval));
}