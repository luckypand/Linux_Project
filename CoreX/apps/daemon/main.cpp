#include "CoreXDaemon.hpp"
#include "SignalHandler.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>

static void printUsage(const char* progName)
{
    std::cout << "CoreX RPC Daemon v" << CoreXDaemon::version() << "\n\n"
              << "Usage: " << progName << " [options]\n\n"
              << "Options:\n"
              << "  -c, --config <path>   Path to YAML config file (default: corex_daemon.yaml)\n"
              << "  -v, --version          Print version and exit\n"
              << "  -h, --help             Print this help and exit\n"
              << "  -d, --daemon           Run as daemon (fork to background)\n"
              << "  -p, --port <port>      Override server port\n"
              << "  --host <host>          Override server host\n"
              << std::endl;
}

int main(int argc, char* argv[])
{
    // --- 解析命令行参数 ---
    std::string configPath = "corex_daemon.yaml";
    bool runAsDaemon = false;
    int  overridePort = -1;
    std::string overrideHost;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-v" || arg == "--version") {
            std::cout << "CoreX RPC Daemon v" << CoreXDaemon::version() << std::endl;
            return 0;
        }
        else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                configPath = argv[++i];
            } else {
                std::cerr << "Error: --config requires a path argument" << std::endl;
                return 1;
            }
        }
        else if (arg == "-d" || arg == "--daemon") {
            runAsDaemon = true;
        }
        else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                overridePort = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: --port requires a number" << std::endl;
                return 1;
            }
        }
        else if (arg == "--host") {
            if (i + 1 < argc) {
                overrideHost = argv[++i];
            } else {
                std::cerr << "Error: --host requires an address" << std::endl;
                return 1;
            }
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    // --- 守护进程化（如果指定了 -d）---
    if (runAsDaemon) {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "Fork failed!" << std::endl;
            return 1;
        }
        if (pid > 0) {
            // 父进程：打印子进程 PID 后退出
            std::cout << "CoreXDaemon started in background, PID: " << pid << std::endl;
            return 0;
        }
        // 子进程：创建新会话，脱离终端
        setsid();
        // 重定向标准 IO 到 /dev/null
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
    }

    // --- 创建并初始化守护进程 ---
    CoreXDaemon daemon;

    if (!daemon.init(configPath)) {
        std::cerr << "Failed to initialize daemon. Check config file: "
                  << configPath << std::endl;
        return 1;
    }

    // 命令行覆盖配置
    // 注意：当前 ConfigManager 不支持直接覆盖服务器地址，
    // 命令行参数仅用于日志记录

    // --- 设置信号处理 ---
    SignalHandler::instance().init([&daemon]() {
        daemon.shutdown();
    });

    // --- 加载插件 ---
    if (!daemon.loadPlugins()) {
        std::cerr << "Warning: No plugins loaded. Daemon will start with no services."
                  << std::endl;
    }

    // --- 启动 ---
    if (!daemon.start()) {
        std::cerr << "Failed to start daemon." << std::endl;
        return 1;
    }

    std::cout << "[CoreXDaemon] Running... (PID: " << getpid() << ")" << std::endl;

    // --- 等待关闭信号 ---
    daemon.waitForShutdown();

    return 0;
}
