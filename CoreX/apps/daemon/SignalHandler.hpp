#pragma once
#include <functional>
#include <csignal>

// ============================================================================
// SignalHandler —— 信号处理器
//
// 注册 SIGTERM / SIGINT 回调，支持优雅关闭。
//
// 使用方式：
//   SignalHandler::instance().init([this]() { shutdown(); });
//   // ... 运行服务 ...
//   SignalHandler::instance().wait();  // 阻塞直到收到信号
// ============================================================================
class SignalHandler
{
public:
    using ShutdownCallback = std::function<void()>;

    static SignalHandler& instance();

    // 注册信号处理器，传入关闭回调
    void init(ShutdownCallback callback);

    // 阻塞等待信号（内部使用 pause/sigsuspend）
    void wait();

    // 手动触发关闭（仅在 EventLoop 线程或其他同步上下文调用）
    void triggerShutdown();

    // 查询是否已收到关闭信号
    bool isShuttingDown() const { return shuttingDown_; }

    // 设置关闭超时后强制退出的时间（秒），默认 30
    void setShutdownTimeout(int seconds) { shutdownTimeout_ = seconds; }

private:
    SignalHandler() = default;
    ~SignalHandler() = default;
    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;

    ShutdownCallback callback_;
    volatile bool    shuttingDown_ = false;
    int              shutdownTimeout_ = 30;
};
