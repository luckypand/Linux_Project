// ============================================================================
// RosNodeManager.cpp — ROS 节点生命周期管理器 实现
// ============================================================================

#include "RosNodeManager.hpp"
#include <ros/ros.h>
#include <ros/master.h>
#include <cstring>

RosNodeManager& RosNodeManager::instance()
{
    static RosNodeManager mgr;
    return mgr;
}

RosNodeManager::~RosNodeManager()
{
    shutdown();
}

bool RosNodeManager::init(int argc, char* argv[],
                          const std::string& nodeName, int spinnerThreads)
{
    // 已初始化则直接返回成功（幂等）
    if (initialized_.load()) return true;

    std::lock_guard<std::mutex> lock(mutex_);

    // 双重检查
    if (initialized_.load()) return true;

    nodeName_ = nodeName;

    // ---- 检查 ROS 是否已被其他代码初始化 ----
    if (!ros::isInitialized()) {
        // 构建 argv（如果调用方传入了有效的 argc/argv 则使用，否则构造默认值）
        int    localArgc = (argc > 0) ? argc : 1;
        char** localArgv = nullptr;

        if (argc > 0 && argv != nullptr) {
            localArgv = argv;
        } else {
            // 静态缓冲区用于默认参数（仅在 argc<=0 时使用）
            static char defaultArg0[] = "corex_ros_bridge";
            static char* defaultArgv[] = {defaultArg0, nullptr};
            localArgc = 1;
            localArgv = defaultArgv;
        }

        ros::init(localArgc, localArgv, nodeName_,
                  ros::init_options::NoSigintHandler);
    }

    // ---- 创建 NodeHandle ----
    nh_        = std::make_unique<ros::NodeHandle>();
    privateNh_ = std::make_unique<ros::NodeHandle>("~");

    // ---- 启动 AsyncSpinner ----
    if (spinnerThreads < 1) spinnerThreads = 1;
    spinner_ = std::make_unique<ros::AsyncSpinner>(spinnerThreads);
    spinner_->start();

    initialized_.store(true);

    ROS_INFO("[RosNodeManager] Node '%s' initialized with %d spinner thread(s)",
             nodeName_.c_str(), spinnerThreads);

    return true;
}

void RosNodeManager::shutdown()
{
    if (!initialized_.load()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_.load()) return;

    ROS_INFO("[RosNodeManager] Shutting down node '%s'...", nodeName_.c_str());

    // 先停 Spinner，再释放 NodeHandle
    if (spinner_) {
        spinner_->stop();
        spinner_.reset();
    }

    privateNh_.reset();
    nh_.reset();

    // 注意：不调用 ros::shutdown()，因为可能还有其他代码在使用 ROS
    // 如果需要完全关闭，由调用方决定

    initialized_.store(false);
    ROS_INFO("[RosNodeManager] Node '%s' shutdown complete", nodeName_.c_str());
}

bool RosNodeManager::isMasterConnected()
{
    if (!initialized_.load()) return false;
    return ros::master::check();
}

ros::NodeHandle& RosNodeManager::nh()
{
    // 如果未初始化，延迟初始化（使用默认参数）
    if (!nh_) {
        init(0, nullptr, "corex_ros_bridge", 2);
    }
    return *nh_;
}

ros::NodeHandle& RosNodeManager::privateNh()
{
    if (!privateNh_) {
        init(0, nullptr, "corex_ros_bridge", 2);
    }
    return *privateNh_;
}
