#include "SignalHandler.hpp"
#include <cstring>
#include <unistd.h>

// 全局实例指针（信号处理函数中访问）
static SignalHandler* g_instance = nullptr;

// 信号处理函数（C 风格，仅在信号安全上下文中设置标志）
static void signalHandler(int signo)
{
    // 仅设置标志，实际处理在 SignalHandler::wait() 中
    if (g_instance && (signo == SIGTERM || signo == SIGINT)) {
        // 第二次收到强制退出信号时直接退出
        if (g_instance->isShuttingDown()) {
            _exit(1);
        }
        g_instance->triggerShutdown();
    }
}

SignalHandler& SignalHandler::instance()
{
    static SignalHandler inst;
    g_instance = &inst;
    return inst;
}

void SignalHandler::init(ShutdownCallback callback)
{
    callback_ = std::move(callback);

    // 注册信号处理函数
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sa.sa_flags   = 0;  // 不设置 SA_RESTART，让系统调用返回 EINTR

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    // 忽略 SIGPIPE（防止写已关闭的 socket 导致进程退出）
    signal(SIGPIPE, SIG_IGN);
}

void SignalHandler::wait()
{
    // 使用 sigsuspend 等待信号，避免忙等
    sigset_t mask, oldMask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);

    // 阻塞信号，等待时原子性地解除阻塞
    sigprocmask(SIG_BLOCK, &mask, &oldMask);

    while (!shuttingDown_) {
        // sigsuspend 临时恢复 oldMask 并等待信号
        sigsuspend(&oldMask);
    }

    // 恢复信号掩码
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}

void SignalHandler::triggerShutdown()
{
    shuttingDown_ = true;
    if (callback_) {
        callback_();
    }
}
