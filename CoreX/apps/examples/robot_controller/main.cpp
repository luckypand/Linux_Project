// ============================================================================
// host_controller.cpp — 机器人遥控主机程序
//
// 功能：连接机器人 CoreXDaemon，轮询遥测数据，发送运动控制指令
//
// 用法：
//   ./host_controller --host 192.168.1.100 --port 8080
//
// 交互命令（运行时输入）：
//   f <speed>    前进 (m/s)，例: f 0.5
//   b <speed>    后退 (m/s)，例: b 0.3
//   l <speed>    左转 (rad/s)，例: l 0.5
//   r <speed>    右转 (rad/s)，例: r 0.5
//   s            急停（全部归零）
//   q            退出
//
// 输入格式支持组合：
//   f 0.3 l 0.2  →  前进 0.3 m/s 同时左转 0.2 rad/s
// ============================================================================

#include "RpcClient.hpp"
#include "../../../proto/robot_service.pb.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <cmath>
#include <csignal>
#include <sys/select.h>
#include <unistd.h>

using namespace std;

// ============================================================================
// 全局状态
// ============================================================================
static atomic<bool> g_running{true};

struct CommandState {
    double linear_x  = 0.0;   // 前进速度
    double angular_z = 0.0;   // 旋转速度
    bool   updated   = false;
};
static CommandState g_cmd;
static mutex        g_cmdMutex;

// ============================================================================
// 信号处理
// ============================================================================
static void onSignal(int)
{
    g_running.store(false);
}

// ============================================================================
// 键盘输入线程：解析用户指令
// ============================================================================
static void inputThread()
{
    cout << "\n[Input] 命令: f/b <speed> | l/r <speed> | s(停) | q(退出)\n";
    cout << "[Input] 示例: f 0.5 l 0.3   (前进+左转)\n\n";
    cout << "> " << flush;

    string line;
    while (g_running.load() && getline(cin, line)) {
        if (line.empty()) { cout << "> " << flush; continue; }

        istringstream iss(line);
        string cmd;
        double linear  = 0.0;
        double angular = 0.0;

        while (iss >> cmd) {
            if (cmd == "q" || cmd == "quit") {
                g_running.store(false);
                return;
            }
            else if (cmd == "s" || cmd == "stop") {
                linear  = 0.0;
                angular = 0.0;
                break;  // stop 覆盖一切
            }
            else if (cmd == "f") {
                double v = 0.0;
                if (iss >> v) linear = v;
            }
            else if (cmd == "b") {
                double v = 0.0;
                if (iss >> v) linear = -v;
            }
            else if (cmd == "l") {
                double v = 0.0;
                if (iss >> v) angular = v;
            }
            else if (cmd == "r") {
                double v = 0.0;
                if (iss >> v) angular = -v;
            }
            else {
                cout << "[Input] 未知命令: " << cmd << endl;
            }
        }

        {
            lock_guard<mutex> lock(g_cmdMutex);
            g_cmd.linear_x  = linear;
            g_cmd.angular_z = angular;
            g_cmd.updated   = true;
        }

        cout << "[Input] 设定: linear=" << fixed << setprecision(2) << linear
             << " angular=" << angular << endl;
        cout << "> " << flush;
    }
}

// ============================================================================
// 打印遥测数据
// ============================================================================
static void printTelemetry(const CoreX::rpc::OdometryResponse& odom,
                           const CoreX::rpc::StatusResponse&  status)
{
    // 清屏并打印表头
    cout << "\033[2J\033[H";  // ANSI: 清屏 + 光标到顶
    cout << "╔══════════════════════════════════════════════════════════════╗\n";
    cout << "║              CoreX 机器人遥控面板                           ║\n";
    cout << "╠══════════════════════════════════════════════════════════════╣\n";

    if (odom.success()) {
        cout << "║  位置 (m):     x=" << setw(8) << fixed << setprecision(3) << odom.pos_x()
             << "  y=" << setw(8) << odom.pos_y()
             << "  z=" << setw(8) << odom.pos_z() << "       ║\n";
        cout << "║  姿态 (四元数): x=" << setw(6) << odom.orient_x()
             << "  y=" << setw(6) << odom.orient_y()
             << "  z=" << setw(6) << odom.orient_z()
             << "  w=" << setw(6) << odom.orient_w() << "  ║\n";
        cout << "║  线速度 (m/s):  x=" << setw(8) << odom.vel_x()
             << "  y=" << setw(8) << odom.vel_y()
             << "  z=" << setw(8) << odom.vel_z() << "       ║\n";
        cout << "║  角速度 (rad/s):x=" << setw(8) << odom.ang_x()
             << "  y=" << setw(8) << odom.ang_y()
             << "  z=" << setw(8) << odom.ang_z() << "       ║\n";
    } else {
        cout << "║  [遥测] 无里程计数据（/odom 未发布或插件未订阅）             ║\n";
    }

    cout << "╠══════════════════════════════════════════════════════════════╣\n";
    cout << "║  电池: " << setw(6) << fixed << setprecision(1) << status.battery_v()
         << "V (" << setw(5) << status.battery_pct() << "%)";

    if (status.emergency_stop()) {
        cout << "  ⚠ 急停！";
    }
    cout << "                                      ║\n";

    cout << "║  状态: " << status.status_msg();
    for (size_t i = status.status_msg().size(); i < 51; i++) cout << ' ';
    cout << "║\n";

    {
        lock_guard<mutex> lock(g_cmdMutex);
        cout << "╠══════════════════════════════════════════════════════════════╣\n";
        cout << "║  当前指令: linear=" << setw(6) << g_cmd.linear_x
             << " m/s  angular=" << setw(6) << g_cmd.angular_z
             << " rad/s       ║\n";
    }

    cout << "╚══════════════════════════════════════════════════════════════╝\n";
    cout << "\n命令 (f/b/l/r/s/q) > " << flush;
}

// ============================================================================
// 发送运动控制指令
// ============================================================================
static void sendCommand(RpcClient& client)
{
    CommandState cmd;
    {
        lock_guard<mutex> lock(g_cmdMutex);
        if (!g_cmd.updated) return;
        cmd = g_cmd;
        g_cmd.updated = false;
    }

    bool isZero = (fabs(cmd.linear_x) < 0.001 && fabs(cmd.angular_z) < 0.001);

    CoreX::rpc::VelocityCommand req;
    req.set_linear_x(cmd.linear_x);
    req.set_angular_z(cmd.angular_z);

    CoreX::rpc::ControlResponse resp;

    std::string method = isZero ? "Stop" : "SetVelocity";

    if (client.call("MotionControl", method, req, resp)) {
        if (!resp.success()) {
            cerr << "[Control] 指令失败: " << resp.error_msg() << endl;
        }
    } else {
        cerr << "[Control] RPC 调用失败，检查连接" << endl;
    }
}

// ============================================================================
// 帮助信息
// ============================================================================
static void printUsage(const char* prog)
{
    cout << "用法: " << prog << " --host <ip> [--port <port>]\n\n"
         << "选项:\n"
         << "  --host <ip>      机器人 IP 地址 (必需)\n"
         << "  --port <port>    CoreXDaemon 端口 (默认: 8080)\n"
         << "  --help           打印帮助\n"
         << "\n交互命令:\n"
         << "  f <speed>        前进 (m/s)\n"
         << "  b <speed>        后退 (m/s)\n"
         << "  l <speed>        左转 (rad/s)\n"
         << "  r <speed>        右转 (rad/s)\n"
         << "  s                急停\n"
         << "  q                退出\n"
         << flush;
}

// ============================================================================
// main
// ============================================================================
int main(int argc, char* argv[])
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // ---- 解析参数 ----
    string host = "";
    uint16_t port = 8080;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--host") {
            if (++i < argc) host = argv[i];
        } else if (arg == "--port") {
            if (++i < argc) port = static_cast<uint16_t>(stoi(argv[i]));
        } else {
            cerr << "未知参数: " << arg << endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (host.empty()) {
        cerr << "错误: 必须指定 --host\n";
        printUsage(argv[0]);
        return 1;
    }

    // ---- 信号处理 ----
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGPIPE, SIG_IGN);

    // ---- 连接机器人 ----
    cout << "[Main] 连接机器人 " << host << ":" << port << " ..." << endl;

    RpcClient client(host, port);
    if (!client.connect()) {
        cerr << "[Main] 连接失败！请确认：\n"
             << "       1. 机器人 CoreXDaemon 已启动\n"
             << "       2. 主机已连接机器人热点\n"
             << "       3. IP 地址正确 (当前: " << host << ")\n";
        return 1;
    }

    cout << "[Main] 已连接！开始遥测轮询...\n" << endl;

    // ---- 启动输入线程 ----
    thread inputTh(inputThread);

    // ---- 主循环：20Hz 轮询遥测 + 发送指令 ----
    CoreX::rpc::OdometryRequest  odomReq;
    CoreX::rpc::OdometryResponse odomResp;
    CoreX::rpc::StatusRequest    statusReq;
    CoreX::rpc::StatusResponse   statusResp;

    int pollSeq = 0;

    while (g_running.load() && client.isConnected()) {
        pollSeq++;

        // --- 每 5 次轮询刷新一次遥测显示 (4Hz 显示, 20Hz 控制) ---
        bool showDisplay = (pollSeq % 5 == 1);

        // 轮询里程计
        if (!client.call("RobotTelemetry", "GetOdometry", odomReq, odomResp)) {
            cerr << "[Main] GetOdometry 失败，尝试重连..." << endl;
            client.disconnect();
            break;
        }

        // 轮询状态
        if (!client.call("RobotTelemetry", "GetStatus", statusReq, statusResp)) {
            cerr << "[Main] GetStatus 失败" << endl;
            // 状态失败不 fatal，继续
        }

        // 显示
        if (showDisplay) {
            printTelemetry(odomResp, statusResp);
        }

        // 发送控制指令
        sendCommand(client);

        // 50ms = 20Hz
        usleep(50000);
    }

    // ---- 清理 ----
    g_running.store(false);
    client.disconnect();

    if (inputTh.joinable()) {
        // inputThread 在 getline 上阻塞，需要用换行符或用 pthread_cancel
        pthread_cancel(inputTh.native_handle());
        inputTh.join();
    }

    // 确保机器人停下
    cout << "\n[Main] 发送急停指令..." << endl;
    RpcClient stopClient(host, port);
    if (stopClient.connect()) {
        CoreX::rpc::VelocityCommand stopReq;  // 全零
        CoreX::rpc::ControlResponse stopResp;
        stopClient.call("MotionControl", "Stop", stopReq, stopResp);
    }

    cout << "[Main] 已退出。" << endl;
    return 0;
}
