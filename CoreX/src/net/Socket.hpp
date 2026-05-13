#pragma once
#include <unistd.h>
#include <sys/socket.h>
#include <utility>

class Socket {
public:
    explicit Socket(int sockfd) : sockfd_(sockfd) {}
    ~Socket() { ::close(sockfd_); } // RAII 核心

    // 禁用拷贝，允许移动 (文件描述符的独占性)
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    //允许移动构造，但是sockfd_本质是一个内置变量，因此只是一个普通赋值，需要防止fd被关闭两次
    Socket(Socket&& rhs) noexcept:sockfd_(std::exchange(rhs.sockfd_,-1)) {}
    Socket& operator=(Socket&& rhs) noexcept;

    int fd() const { return sockfd_; } 
    void bindAddress(int port);
    void listen();
    
    // 接收新连接，并通过 peeraddr 传出客户端的真实 IP 和端口
    // 返回的是已经设置为 非阻塞(NONBLOCK) 和 退出时关闭(CLOEXEC) 的新 fd
    int accept(struct sockaddr_in* peeraddr);

    // --- 核心 Socket 调优选项 ---
    void setReuseAddr(bool on);   // 解决 TIME_WAIT 端口占用问题
    void setReusePort(bool on);   // 支持内核级负载均衡 (多线程绑定同端口)
    void setTcpNoDelay(bool on);  // 禁用 Nagle 算法，降低 RPC 延迟
    void setKeepAlive(bool on);   // 开启 TCP 层面的心跳保活机制

    //拓展：setKeepAlive本质上是利用系统自带的2小时心跳包，对于高性能服务器
    //应该自己实现心跳检测机制，保证服务器的高性能

private:
    int sockfd_;
};