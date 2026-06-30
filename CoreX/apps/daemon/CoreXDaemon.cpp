#include "CoreXDaemon.hpp"
#include "ConfigManager.hpp"
#include "PluginLoader.hpp"
#include "SignalHandler.hpp"
#include "../../src/net/EventLoop.hpp"
#include "../../src/rpc/RpcServer.hpp"
#include "../../src/rpc/RpcServiceAdapter.hpp"
#include "../../src/log/my_log.hpp"
#include <iostream>
#include <cstdio>
#include <algorithm>

// 版本号
#define COREX_VERSION_MAJOR 1
#define COREX_VERSION_MINOR 0
#define COREX_VERSION_PATCH 0

CoreXDaemon::CoreXDaemon()
    : config_(std::make_shared<ConfigManager>())
    , pluginLoader_(std::make_unique<PluginLoader>())
{
}

CoreXDaemon::~CoreXDaemon()
{
    shutdown();
}

bool CoreXDaemon::init(const std::string& configPath)
{
    // --- 加载配置 ---
    if (!config_->load(configPath)) {
        std::cerr << "[CoreXDaemon] ERROR: Failed to load config: "
                  << config_->getLastError() << std::endl;
        return false;
    }

    // --- 初始化日志系统 ---
    std::string logLevel = config_->getString("logging.level", "INFO");
    std::string logPath  = config_->getString("logging.path", "./log");
    int logLevelInt = 0;
    if (logLevel == "DEBUG") logLevelInt = 0;
    else if (logLevel == "INFO")  logLevelInt = 1;
    else if (logLevel == "WARN")  logLevelInt = 2;
    else if (logLevel == "ERROR") logLevelInt = 3;
    else if (logLevel == "FATAL") logLevelInt = 4;
    else logLevelInt = 1;

#if ENABLE_LOG
    Log::Instance()->Init(logLevelInt, logPath, 50000, "corex_daemon");
    LOG_INFO("[CoreXDaemon] Log system initialized, level=%s, path=%s",
             logLevel.c_str(), logPath.c_str());
#endif

    // --- 创建主 EventLoop ---
    mainLoop_ = std::make_unique<EventLoop>();

    // --- 读取服务器配置 ---
    std::string host = config_->getString("server.host", "0.0.0.0");
    int port = config_->getInt("server.port", 8080);
    int workerThreads = config_->getInt("server.worker_threads", 4);
    std::string serverName = config_->getString("server.name", "CoreXDaemon");

    // --- 创建 RpcServer ---
    rpcServer_ = std::make_unique<RpcServer>(mainLoop_.get(), host,
                                              static_cast<uint16_t>(port), serverName);

    // 设置延迟统计（如果启用）
    bool latencyStats = config_->getBool("monitoring.latency_stats", false);
    if (latencyStats) {
        // RpcLatencyStats 由 RpcServer 内部管理
        // 外部可以设置 stats 对象，此处暂不实现独立的管理
    }

#if ENABLE_LOG
    LOG_INFO("[CoreXDaemon] Configuration loaded: host=%s, port=%d, workers=%d",
             host.c_str(), port, workerThreads);
#endif

    return true;
}

bool CoreXDaemon::loadPlugins()
{
    std::string pluginDir = config_->getString("plugins.directory", "./plugins");
    bool autoLoad = config_->getBool("plugins.autoload", true);

    if (!autoLoad) {
        std::cout << "[CoreXDaemon] Plugin autoload disabled, skipping." << std::endl;
        return true;
    }

    std::vector<RpcServiceAdapter*> services = pluginLoader_->loadDirectory(pluginDir);

    if (services.empty()) {
        std::cout << "[CoreXDaemon] WARNING: No plugins loaded from directory: "
                  << pluginDir << std::endl;
        if (!pluginLoader_->getLastError().empty()) {
            std::cerr << "[CoreXDaemon] Plugin loader error: "
                      << pluginLoader_->getLastError() << std::endl;
        }
        return false;
    }

    // --- 注册所有加载的服务 ---
    for (auto* svc : services) {
        rpcServer_->registerService(svc);
        std::cout << "[CoreXDaemon] Registered service: " << svc->serviceName() << std::endl;
#if ENABLE_LOG
        LOG_INFO("[CoreXDaemon] Registered service: %s", svc->serviceName().c_str());
#endif
    }

    return true;
}

bool CoreXDaemon::start()
{
    if (started_) return true;

    // --- 启动 RPC 服务器（开始监听端口） ---
    rpcServer_->start();

    // --- 启用 IPC fast-path（如果配置了） ---
    bool ipcEnabled = config_->getBool("ipc.enabled", true);
    if (ipcEnabled) {
        std::string shmName = config_->getString("ipc.shm_name", "/corex_rpc_ipc");
        rpcServer_->enableIpc(shmName);
#if ENABLE_LOG
        LOG_INFO("[CoreXDaemon] IPC fast-path enabled on shm: %s", shmName.c_str());
#endif
    }

    // --- 在独立线程中运行 EventLoop ---
    running_ = true;
    eventLoopThread_ = std::thread([this]() {
        this->runEventLoop();
    });

    started_ = true;
    printBanner();

    return true;
}

void CoreXDaemon::runEventLoop()
{
    // EventLoop::loop() 会阻塞直到 quit() 被调用
    mainLoop_->loop();
}

void CoreXDaemon::waitForShutdown()
{
    if (!running_) return;

    // 阻塞等待信号
    SignalHandler::instance().wait();
}

void CoreXDaemon::shutdown()
{
    if (!running_) return;

    std::cout << "\n[CoreXDaemon] Shutting down..." << std::endl;
#if ENABLE_LOG
    LOG_INFO("[CoreXDaemon] Shutdown initiated");
#endif

    running_ = false;

    // 1. 停止 EventLoop
    if (mainLoop_) {
        mainLoop_->quit();
    }

    // 2. 等待 EventLoop 线程退出
    if (eventLoopThread_.joinable()) {
        eventLoopThread_.join();
    }

    // 3. 卸载所有插件
    pluginLoader_->unloadAll();

#if ENABLE_LOG
    LOG_INFO("[CoreXDaemon] Shutdown complete");
#endif
    std::cout << "[CoreXDaemon] Shutdown complete." << std::endl;
}

std::vector<std::string> CoreXDaemon::getServiceNames() const
{
    std::vector<std::string> names;
    for (const auto& plugin : pluginLoader_->getPlugins()) {
        if (plugin.service) {
            names.push_back(plugin.service->serviceName());
        }
    }
    return names;
}

std::string CoreXDaemon::version()
{
    return std::to_string(COREX_VERSION_MAJOR) + "." +
           std::to_string(COREX_VERSION_MINOR) + "." +
           std::to_string(COREX_VERSION_PATCH);
}

void CoreXDaemon::printBanner() const
{
    std::string host = config_->getString("server.host", "0.0.0.0");
    int port = config_->getInt("server.port", 8080);

    std::cout << "\n"
              << "╔══════════════════════════════════════════════╗\n"
              << "║        CoreX RPC Daemon v" << version() << "                ║\n"
              << "╠══════════════════════════════════════════════╣\n"
              << "║  TCP  : " << host << ":" << port;

    // 对齐
    std::string addr = host + ":" + std::to_string(port);
    for (size_t i = addr.size(); i < 22; i++) std::cout << " ";
    std::cout << "║\n";

    bool ipcEnabled = config_->getBool("ipc.enabled", true);
    if (ipcEnabled) {
        std::string shm = config_->getString("ipc.shm_name", "/corex_rpc_ipc");
        std::cout << "║  IPC  : " << shm;
        for (size_t i = shm.size(); i < 22; i++) std::cout << " ";
        std::cout << "║\n";
    }

    auto services = getServiceNames();
    std::cout << "║  Services: " << services.size() << " loaded";
    for (size_t i = 18 + std::to_string(services.size()).size(); i < 34; i++) std::cout << " ";
    std::cout << "║\n";

    for (const auto& name : services) {
        std::cout << "║    - " << name;
        for (size_t i = name.size() + 6; i < 44; i++) std::cout << " ";
        std::cout << "║\n";
    }

    std::cout << "╚══════════════════════════════════════════════╝\n"
              << std::endl;
}
