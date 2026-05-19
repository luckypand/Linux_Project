// 网络库整体集成测试
// 测试内容：Buffer、Socket、Channel、EventLoop、TcpConnection、Acceptor 的协作

#include <cassert>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <poll.h>
#include <string>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "../src/net/Acceptor.hpp"
#include "../src/net/Buffer.hpp"
#include "../src/net/Channel.hpp"
#include "../src/net/EventLoop.hpp"
#include "../src/net/Socket.hpp"

namespace
{
struct SocketOptionCheck
{
    int level;
    int optname;
    int expected;
};

bool waitReadable(int fd, std::chrono::milliseconds timeout)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    return ret > 0 && (pfd.revents & POLLIN);
}

int createSocketPair(int fds[2])
{
    return ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds);
}

void verifySocketOption(int fd, const SocketOptionCheck& option)
{
    int value = -1;
    socklen_t len = sizeof(value);
    int ret = ::getsockopt(fd, option.level, option.optname, &value, &len);
    assert(ret == 0);
    assert(value == option.expected);
}
}

// ============ 测试 1: Buffer 基本功能 ============
void testBuffer()
{
    std::cout << "\n=== Test 1: Buffer 基本功能 ===" << std::endl;

    Buffer buffer(64);

    buffer.append("Hello", 5);
    buffer.append(std::string(", World!"));
    assert(buffer.ReadBytes() == 13);
    assert(std::string(buffer.peek(), buffer.ReadBytes()) == "Hello, World!");
    std::cout << "[PASS] append 和 peek" << std::endl;

    buffer.retrieve(7);
    assert(buffer.ReadBytes() == 6);
    assert(std::string(buffer.peek(), buffer.ReadBytes()) == "World!");
    std::cout << "[PASS] retrieve 消费字节" << std::endl;

    assert(buffer.retrieveAsString(5) == "World");
    assert(buffer.ReadBytes() == 1);
    assert(buffer.retrieveAsString() == "!");
    assert(buffer.ReadBytes() == 0);
    std::cout << "[PASS] retrieveAsString" << std::endl;

    const char prefix[] = "TCP ";
    buffer.append("Echo", 4);
    buffer.prepend(prefix, sizeof(prefix) - 1);
    assert(std::string(buffer.peek(), buffer.ReadBytes()) == "TCP Echo");
    std::cout << "[PASS] prepend" << std::endl;

    buffer.clear();
    assert(buffer.ReadBytes() == 0);
    std::cout << "[PASS] clear" << std::endl;

    std::cout << "[PASS] Buffer 测试通过\n" << std::endl;
}

// ============ 测试 2: Socket 创建和配置 ============
void testSocket()
{
    std::cout << "=== Test 2: Socket 创建和配置 ===" << std::endl;

    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    assert(sockfd >= 0);

    Socket socket(sockfd);
    assert(socket.fd() == sockfd);
    std::cout << "[PASS] Socket 创建" << std::endl;

    socket.setReuseAddr(true);
    socket.setReusePort(true);
    socket.setTcpNoDelay(true);
    socket.setKeepAlive(true);

    verifySocketOption(sockfd, {SOL_SOCKET, SO_REUSEADDR, 1});
    verifySocketOption(sockfd, {SOL_SOCKET, SO_REUSEPORT, 1});
    verifySocketOption(sockfd, {IPPROTO_TCP, TCP_NODELAY, 1});
    verifySocketOption(sockfd, {SOL_SOCKET, SO_KEEPALIVE, 1});
    std::cout << "[PASS] Socket 选项配置" << std::endl;

    std::cout << "[PASS] Socket 测试通过\n" << std::endl;
}

// ============ 测试 3: Acceptor 烟雾测试 ============
void testAcceptorSmoke()
{
    std::cout << "=== Test 3: Acceptor 烟雾测试 ===" << std::endl;

    std::promise<void> readyPromise;
    auto readyFuture = readyPromise.get_future();

    std::thread serverThread([&]() {
        EventLoop loop;
        Acceptor acceptor(&loop, 0);
        acceptor.listen();

        readyPromise.set_value();
        loop.quit();
        loop.loop();
    });

    readyFuture.wait();
    serverThread.join();

    std::cout << "[PASS] Acceptor 构造和 listen 可正常执行" << std::endl;
    std::cout << "[PASS] Acceptor 测试通过\n" << std::endl;
}

// ============ 测试 4: Channel 事件回调 ============
void testChannel()
{
    std::cout << "=== Test 4: Channel 事件回调 ===" << std::endl;

    int fds[2] = {-1, -1};
    assert(createSocketPair(fds) == 0);

    std::promise<std::string> receivedPromise;
    auto receivedFuture = receivedPromise.get_future();
    std::promise<void> readyPromise;
    auto readyFuture = readyPromise.get_future();

    std::thread serverThread([&]() {
        EventLoop loop;
        Channel channel(&loop, fds[0]);

        channel.setReadCallback([&]() {
            char buffer[128] = {0};
            ssize_t n = ::read(fds[0], buffer, sizeof(buffer));
            assert(n > 0);
            receivedPromise.set_value(std::string(buffer, static_cast<size_t>(n)));
            loop.RemoveChannel(&channel);
            loop.quit();
        });

        channel.enableReading();
        readyPromise.set_value();
        loop.loop();
    });

    readyFuture.wait();

    const char* message = "channel event";
    ssize_t written = ::write(fds[1], message, std::strlen(message));
    assert(written == static_cast<ssize_t>(std::strlen(message)));

    assert(receivedFuture.get() == message);
    serverThread.join();

    ::close(fds[0]);
    ::close(fds[1]);

    std::cout << "[PASS] Channel 读事件回调" << std::endl;
    std::cout << "[PASS] Channel 测试通过\n" << std::endl;
}

// ============ 测试 5: EventLoop 待处理任务执行 ============
void testEventLoop()
{
    std::cout << "=== Test 5: EventLoop 待处理任务 ===" << std::endl;

    std::promise<EventLoop*> loopPromise;
    auto loopFuture = loopPromise.get_future();
    std::atomic<bool> executed{false};

    std::thread loopThread([&]() {
        EventLoop loop;
        assert(loop.IsInloopthread());
        loopPromise.set_value(&loop);
        loop.loop();
    });

    EventLoop* loop = loopFuture.get();
    loop->SengToPending([&]() { executed = true; });
    loop->SengToPending([&]() { loop->quit(); });

    loopThread.join();

    assert(executed.load());
    std::cout << "[PASS] IsInloopthread 检查" << std::endl;
    std::cout << "[PASS] 待处理任务执行" << std::endl;
    std::cout << "[PASS] EventLoop 测试通过\n" << std::endl;
}

// ============ 测试 6: 手写回显服务器集成 ============
void testEchoServer()
{
    std::cout << "=== Test 6: 手写回显服务器集成 ===" << std::endl;

    std::promise<int> portPromise;
    auto portFuture = portPromise.get_future();
    std::promise<void> readyPromise;
    auto readyFuture = readyPromise.get_future();

    std::thread serverThread([&]() {
        EventLoop loop;

        int listenFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        assert(listenFd >= 0);

        int optval = 1;
        int ret = ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        assert(ret == 0);

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);

        ret = ::bind(listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        assert(ret == 0);

        ret = ::listen(listenFd, SOMAXCONN);
        assert(ret == 0);

        socklen_t addrLen = sizeof(addr);
        ret = ::getsockname(listenFd, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
        assert(ret == 0);
        int port = ntohs(addr.sin_port);

        Channel listenChannel(&loop, listenFd);
        listenChannel.setReadCallback([&]() {
            struct sockaddr_in peerAddr;
            socklen_t peerLen = sizeof(peerAddr);
            int clientFd = ::accept4(listenFd, reinterpret_cast<struct sockaddr*>(&peerAddr), &peerLen,
                                     SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (clientFd < 0)
            {
                return;
            }

            Channel* clientChannel = new Channel(&loop, clientFd);
            clientChannel->setReadCallback([&loop, clientChannel, clientFd]() {
                char buffer[1024];
                ssize_t n = ::read(clientFd, buffer, sizeof(buffer));
                if (n > 0)
                {
                    ssize_t wrote = ::write(clientFd, buffer, static_cast<size_t>(n));
                    assert(wrote == n);
                }
                else
                {
                    loop.RemoveChannel(clientChannel);
                    delete clientChannel;
                    ::close(clientFd);
                    loop.quit();
                }
            });
            clientChannel->enableReading();
        });

        listenChannel.enableReading();
        portPromise.set_value(port);
        readyPromise.set_value();
        loop.loop();

        loop.RemoveChannel(&listenChannel);
        ::close(listenFd);
    });

    readyFuture.wait();
    int port = portFuture.get();

    int clientFd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(clientFd >= 0);

    struct sockaddr_in connectAddr;
    std::memset(&connectAddr, 0, sizeof(connectAddr));
    connectAddr.sin_family = AF_INET;
    connectAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connectAddr.sin_port = htons(port);

    int ret = ::connect(clientFd, reinterpret_cast<struct sockaddr*>(&connectAddr), sizeof(connectAddr));
    assert(ret == 0);

    const char* message = "Hello, Server!";
    ssize_t wrote = ::write(clientFd, message, std::strlen(message));
    assert(wrote == static_cast<ssize_t>(std::strlen(message)));

    assert(waitReadable(clientFd, std::chrono::milliseconds(1000)));
    char reply[128] = {0};
    ssize_t replyLen = ::read(clientFd, reply, sizeof(reply));
    assert(replyLen == static_cast<ssize_t>(std::strlen(message)));
    assert(std::string(reply, static_cast<size_t>(replyLen)) == message);

    ::close(clientFd);
    serverThread.join();

    std::cout << "[PASS] Socket 绑定" << std::endl;
    std::cout << "[PASS] Socket 监听" << std::endl;
    std::cout << "[PASS] 客户端连接" << std::endl;
    std::cout << "[PASS] 数据正确回显" << std::endl;
    std::cout << "[PASS] 回显服务器测试通过\n" << std::endl;
}

int main()
{
    std::cout << "\n╔════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║       网络库整体集成测试 (Test Suite)         ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════╝" << std::endl;

    try
    {
        testBuffer();
        testSocket();
        testAcceptorSmoke();
        testChannel();
        testEventLoop();
        testEchoServer();

        std::cout << "╔════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║              全部测试通过！                   ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════════╝\n" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FAIL] 测试失败: " << e.what() << std::endl;
        return 1;
    }
}
