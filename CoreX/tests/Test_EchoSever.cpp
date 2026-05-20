#include "../src/net/TcpServer.hpp"
#include "../src/net/EventLoop.hpp"
#include <iostream>

class EchoServer {
public:
    EchoServer(EventLoop* loop, const std::string& ip, uint16_t port, int numThreads)
        : server_(loop, ip, port, "EchoServer") {
        // 核心：开启 4 个子 Reactor 线程
        server_.setThreadNum(numThreads);
        
        // 绑定阶段一打通的生命周期回调
        server_.setConnectionCallback(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        server_.setMessageCallback(std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2));
    }

    void start() { server_.start(); }

private:
    void onConnection(const TcpServer::TcpConnectionPtr& conn) {
        if (conn->connected()) {
            // 连接建立，可选择打印日志（海量并发测试时建议关闭打印）
        } else {
            // 连接断开，触发 TcpConnection 的析构与清理
        }
    }

    void onMessage(const TcpServer::TcpConnectionPtr& conn, Buffer& buf) {
        // 极限 Echo：读出来，塞回去
        std::string msg = buf.retrieveAsString();
        conn->send(msg); 
    }

    TcpServer server_;
};

int main() {
    EventLoop loop; // Main Reactor
    EchoServer server(&loop, "0.0.0.0", 8080, 4); // 4 个 Sub Reactor
    server.start();
    loop.loop();
    return 0;
}