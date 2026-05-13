// 单线程超时踢人空壳 Server 测试
// 基于 EventLoop + HeapTimer + Poller + Channel
//
// 测试方法:
//   1. 启动服务器，监听 9999 端口
//   2. 用 telnet/nc 连接:  nc localhost 9999
//   3. 连接后 5 秒内不发送数据，会被自动踢掉
//   4. 发送数据后，定时器重置，再等 5 秒
//   5. 按 Ctrl+C 可退出服务器

#include "../src/net/EventLoop.hpp"
#include "../src/net/Channel.hpp"
#include "../src/net/HeapTimer.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include <iostream>
#include <memory>
#include <unordered_map>
#include <string>

const int LISTEN_PORT      = 9999;
const int IDLE_TIMEOUT_MS  = 5000;   // 5 秒无数据则踢掉

// 全局指针，方便在回调中使用
static EventLoop*                          g_loop  = nullptr;
static HeapTimer*                          g_timer = nullptr;
static std::unordered_map<int, Channel*>   g_clients;   // fd -> Channel*

// ---------- 工具函数 ----------

static std::string sockaddr_to_string(const struct sockaddr_in& addr)
{
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

// 统一清理客户端: 从 epoll 移除 -> 关闭 fd -> 删除 Channel -> 从 map 移除
static void cleanupClient(int fd, const char* reason)
{
    std::cout << "[" << reason << "] fd=" << fd << std::endl;

    auto it = g_clients.find(fd);
    if (it != g_clients.end())
    {
        Channel* ch = it->second;
        g_loop->RemoveChannel(ch);
        delete ch;
        g_clients.erase(it);
    }
    ::close(fd);
}

// ---------- 定时器超时回调 ----------
// 由 HeapTimer::tick() 或 doWork() 触发
static auto makeTimeoutCallback(int fd)
{
    return [fd]() {
        cleanupClient(fd, "TIMEOUT");
    };
}

// ---------- 客户端可读回调 ----------
static void onClientRead(Channel* channel)
{
    int fd = channel->Fd();
    char buf[4096];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);

    if (n > 0)
    {
        buf[n] = '\0';
        // 去掉尾部换行（如果有）
        if (n > 0 && buf[n - 1] == '\n') buf[--n] = '\0';
        if (n > 0 && buf[n - 1] == '\r') buf[--n] = '\0';

        std::cout << "fd=" << fd << " recv " << n << " bytes: " << buf << std::endl;

        // 收到数据，重置定时器
        g_timer->adjust(fd, IDLE_TIMEOUT_MS);

        // 回显
        std::string reply = "echo: ";
        reply += buf;
        reply += "\n";
        ::write(fd, reply.data(), reply.size());
    }
    else if (n == 0)
    {
        // 客户端正常断开
        g_timer->doWork(fd);  // 执行超时回调，统一清理
    }
    else
    {
        // 读错误
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            g_timer->doWork(fd);
        }
    }
}

// ---------- 监听 socket 可读回调 (accept) ----------
static void onAccept(Channel* listenChannel)
{
    int listenFd = listenChannel->Fd();
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);

    int clientFd = ::accept4(listenFd,
                             (struct sockaddr*)&clientAddr,
                             &clientLen,
                             SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (clientFd < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            std::cerr << "accept error: " << strerror(errno) << std::endl;
        return;
    }

    std::string peer = sockaddr_to_string(clientAddr);
    std::cout << "[CONNECT] fd=" << clientFd << " from " << peer
              << " (timeout=" << IDLE_TIMEOUT_MS << "ms)" << std::endl;

    // 创建客户端的 Channel
    Channel* clientChannel = new Channel(g_loop, clientFd);
    g_clients[clientFd] = clientChannel;

    clientChannel->setReadCallback([clientChannel]() {
        onClientRead(clientChannel);
    });
    clientChannel->enableReading();

    // 加入定时器
    g_timer->add(clientFd, IDLE_TIMEOUT_MS, makeTimeoutCallback(clientFd));
}

// ---------- 创建监听 socket ----------
static int createListenSocket(int port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        std::cerr << "socket() failed: " << strerror(errno) << std::endl;
        return -1;
    }

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "bind() failed: " << strerror(errno) << std::endl;
        ::close(fd);
        return -1;
    }

    if (::listen(fd, SOMAXCONN) < 0)
    {
        std::cerr << "listen() failed: " << strerror(errno) << std::endl;
        ::close(fd);
        return -1;
    }

    return fd;
}

// ---------- 信号处理 (优雅退出) ----------
static int g_listenFd = -1;

static void sigHandler(int)
{
    std::cout << "\n[EXIT] shutting down..." << std::endl;
    if (g_loop)  g_loop->quit();
}

int main()
{
    g_listenFd = createListenSocket(LISTEN_PORT);
    if (g_listenFd < 0) return 1;

    std::cout << "========================================" << std::endl;
    std::cout << "  Single-thread timeout server" << std::endl;
    std::cout << "  Listening on 0.0.0.0:" << LISTEN_PORT << std::endl;
    std::cout << "  Idle timeout: " << IDLE_TIMEOUT_MS << "ms" << std::endl;
    std::cout << "  Try: nc localhost " << LISTEN_PORT << std::endl;
    std::cout << "========================================" << std::endl;

    // 注册信号
    ::signal(SIGINT,  sigHandler);
    ::signal(SIGTERM, sigHandler);

    // 核心组件
    EventLoop loop;
    HeapTimer timer;

    g_loop  = &loop;
    g_timer = &timer;

    // 将定时器注入事件循环
    loop.setTimer(&timer);

    // 监听 Channel
    Channel listenChannel(&loop, g_listenFd);
    listenChannel.setReadCallback([&]() { onAccept(&listenChannel); });
    listenChannel.enableReading();

    // 启动事件循环 (阻塞，直到 quit() 被调用)
    loop.loop();

    // 清理
    std::cout << "Cleaning up " << g_clients.size() << " remaining clients..." << std::endl;
    for (auto& [fd, ch] : g_clients)
    {
        g_loop->RemoveChannel(ch);
        delete ch;
        ::close(fd);
    }
    g_clients.clear();

    ::close(g_listenFd);
    std::cout << "Server stopped." << std::endl;

    return 0;
}
