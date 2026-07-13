#pragma once
// ============================================================================
// RosNodeManager.hpp — ROS 节点生命周期管理器
//
// 职责：
//   1. 封装 ros::init() / ros::NodeHandle 的创建与销毁
//   2. 管理 ros::AsyncSpinner 线程池（可配置线程数）
//   3. 提供 ros::NodeHandle 访问接口给上层适配器
//   4. 支持 ROS Master 连接状态监控（/rosout 心跳检测）
//   5. 单例模式（全局唯一 ROS 节点），线程安全
//
// 使用方式：
//   auto& mgr = RosNodeManager::instance();
//   mgr.init(argc, argv, "corex_ros_bridge", 4);
//   // ... 使用 mgr.nh() 创建 Publisher/Subscriber/ServiceClient ...
//   mgr.shutdown();
// ============================================================================

#include <ros/ros.h>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>

class RosNodeManager
{
public:
    // ---- 单例 ----
    static RosNodeManager& instance();

    // ---- 生命周期 ----
    // 初始化 ROS 节点（仅首次调用生效，后续调用忽略）
    // @param argc, argv  命令行参数（用于 ros::init）
    // @param nodeName    ROS 节点名称
    // @param spinnerThreads  AsyncSpinner 线程数（默认 2）
    // @return true 初始化成功
    bool init(int argc, char* argv[], const std::string& nodeName,
              int spinnerThreads = 2);

    // 关闭 ROS 节点，停止 Spinner，释放 NodeHandle
    void shutdown();

    // ---- 状态查询 ----
    bool isInitialized() const { return initialized_.load(); }
    bool isMasterConnected();

    // ---- ROS 句柄访问 ----
    // 全局 NodeHandle（对应 ros::NodeHandle()）
    ros::NodeHandle& nh();
    // 私有 NodeHandle（对应 ros::NodeHandle("~")）
    ros::NodeHandle& privateNh();

    // 获取 ROS 节点名称
    const std::string& nodeName() const { return nodeName_; }

    // ---- 禁止拷贝 / 移动 ----
    RosNodeManager(const RosNodeManager&) = delete;
    RosNodeManager& operator=(const RosNodeManager&) = delete;

private:
    RosNodeManager() = default;
    ~RosNodeManager();

    std::atomic<bool> initialized_{false};
    std::string       nodeName_;

    std::unique_ptr<ros::NodeHandle>    nh_;
    std::unique_ptr<ros::NodeHandle>    privateNh_;
    std::unique_ptr<ros::AsyncSpinner>  spinner_;

    // dpkg 保护 init/shutdown 竞态
    std::mutex mutex_;
};
