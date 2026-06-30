#pragma once
#include <string>
#include <vector>
#include <memory>
#include <thread>

class EventLoop;
class RpcServer;
class RpcServiceAdapter;
class ConfigManager;
class PluginLoader;

// ============================================================================
// CoreXDaemon —— RPC 中间件守护进程
//
// 生命周期：init() → loadPlugins() → start() → waitForShutdown() → shutdown()
//
// 使用方式：
//   CoreXDaemon daemon;
//   daemon.init("config.yaml");
//   daemon.loadPlugins();
//   daemon.start();
//   daemon.waitForShutdown();   // 阻塞直到收到 SIGTERM/SIGINT
//   daemon.shutdown();
// ============================================================================
class CoreXDaemon
{
public:
    CoreXDaemon();
    ~CoreXDaemon();

    // 禁止拷贝
    CoreXDaemon(const CoreXDaemon&) = delete;
    CoreXDaemon& operator=(const CoreXDaemon&) = delete;

    // 步骤 1: 初始化 —— 加载配置、创建 EventLoop 和 RpcServer
    bool init(const std::string& configPath = "corex_daemon.yaml");

    // 步骤 2: 加载插件 —— 从配置的插件目录加载 .so 文件
    bool loadPlugins();

    // 步骤 3: 启动 —— 启动 RPC 服务器（开始监听端口和 IPC）
    bool start();

    // 步骤 4: 等待关闭信号 —— 阻塞直到 SIGTERM/SIGINT
    void waitForShutdown();

    // 步骤 5: 优雅关闭 —— 停止接受请求、清理资源
    void shutdown();

    // 查询是否正在运行
    bool isRunning() const { return running_; }

    // 获取已注册的服务列表
    std::vector<std::string> getServiceNames() const;

    // 获取版本信息
    static std::string version();

private:
    // 内部辅助方法
    void runEventLoop();        // 在独立线程中运行 EventLoop
    void printBanner() const;   // 打印启动信息

    // 配置
    std::shared_ptr<ConfigManager> config_;

    // 核心组件
    std::unique_ptr<EventLoop>  mainLoop_;
    std::unique_ptr<RpcServer>  rpcServer_;
    std::unique_ptr<PluginLoader> pluginLoader_;

    // 运行时状态
    std::thread  eventLoopThread_;
    bool         running_ = false;
    bool         started_ = false;
};
