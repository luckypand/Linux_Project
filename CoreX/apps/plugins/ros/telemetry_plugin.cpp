// ============================================================================
// telemetry_plugin.cpp — 机器人遥测插件
//
// 功能：订阅 ROS /odom 等 topic，通过 RPC 暴露 GetOdometry / GetStatus
//
// 部署到机器人：
//   编译为 libtelemetry_plugin.so，放入 CoreXDaemon 的 plugins 目录
//   确保 ROS master 已启动（roscore）
//
// 编译依赖（需 ROS 环境）:
//   roscpp, nav_msgs, geometry_msgs
// ============================================================================

#include "../../../src/rpc/RpcServiceAdapter.hpp"
#include "../../../proto/robot_service.pb.h"
#include <ros/ros.h>
#include <nav_msgs/Odometry.h>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>
#include <cstring>

// ============================================================================
// 全局 ROS 初始化（多个插件共享，call_once 保证只初始化一次）
// ============================================================================
static std::once_flag g_ros_init_flag;

static void ensureRosInit()
{
    std::call_once(g_ros_init_flag, []() {
        // dlopen 加载的插件没有 argc/argv，手动构造
        int   argc = 1;
        char  arg0[] = "corex_robot_bridge";
        char* argv[] = {arg0, nullptr};

        ros::init(argc, argv, "corex_robot_bridge",
                  ros::init_options::NoSigintHandler);

        ROS_INFO("[CoreX] ROS initialized for plugin bridge");
    });
}

// ============================================================================
// TelemetryServiceImpl — 订阅 ROS 遥测数据 + 提供 RPC 接口
// ============================================================================
class TelemetryServiceImpl : public RpcServiceAdapter
{
public:
    TelemetryServiceImpl()
        : RpcServiceAdapter(
              // 从生成的 pb 中查找 RobotTelemetry ServiceDescriptor
              CoreX::rpc::OdometryRequest::descriptor()
                  ->file()->FindServiceByName("RobotTelemetry"))
    {
        ensureRosInit();

        // ---- 创建 ROS NodeHandle ----
        nh_ = std::make_unique<ros::NodeHandle>("~");

        // ---- 订阅 /odom topic ----
        odomSub_ = nh_->subscribe("/odom", 1,
            &TelemetryServiceImpl::onOdometry, this);

        // ---- 启动异步 spinner（独立线程 spin ROS 回调）----
        spinner_ = std::make_unique<ros::AsyncSpinner>(1);  // 单线程 spin
        spinner_->start();

        ROS_INFO("[TelemetryPlugin] Subscribed to /odom, service ready");

        // ---- 注册 RPC 方法 ----
        registerHandler("GetOdometry",
            [this](::google::protobuf::Message* /*req*/,
                   ::google::protobuf::Message* resp)
            {
                auto* rsp = static_cast<CoreX::rpc::OdometryResponse*>(resp);
                this->fillOdometry(rsp);
            });

        registerHandler("GetStatus",
            [this](::google::protobuf::Message* /*req*/,
                   ::google::protobuf::Message* resp)
            {
                auto* rsp = static_cast<CoreX::rpc::StatusResponse*>(resp);
                this->fillStatus(rsp);
            });
    }

    ~TelemetryServiceImpl() override
    {
        if (spinner_) spinner_->stop();
        odomSub_.shutdown();
    }

private:
    // ---- ROS 回调：缓存最新里程计数据 ----
    void onOdometry(const nav_msgs::Odometry::ConstPtr& msg)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        lastOdom_.pos_x    = msg->pose.pose.position.x;
        lastOdom_.pos_y    = msg->pose.pose.position.y;
        lastOdom_.pos_z    = msg->pose.pose.position.z;
        lastOdom_.orient_x = msg->pose.pose.orientation.x;
        lastOdom_.orient_y = msg->pose.pose.orientation.y;
        lastOdom_.orient_z = msg->pose.pose.orientation.z;
        lastOdom_.orient_w = msg->pose.pose.orientation.w;
        lastOdom_.vel_x    = msg->twist.twist.linear.x;
        lastOdom_.vel_y    = msg->twist.twist.linear.y;
        lastOdom_.vel_z    = msg->twist.twist.linear.z;
        lastOdom_.ang_x    = msg->twist.twist.angular.x;
        lastOdom_.ang_y    = msg->twist.twist.angular.y;
        lastOdom_.ang_z    = msg->twist.twist.angular.z;

        hasOdom_ = true;
    }

    // ---- 填充 OdometryResponse（线程安全）----
    void fillOdometry(CoreX::rpc::OdometryResponse* rsp)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (hasOdom_) {
            rsp->CopyFrom(lastOdom_);
            rsp->set_success(true);
        } else {
            rsp->set_success(false);
        }
    }

    // ---- 填充 StatusResponse（当前为模拟数据，可按需扩展）----
    void fillStatus(CoreX::rpc::StatusResponse* rsp)
    {
        rsp->set_battery_v(12.0);
        rsp->set_battery_pct(85.0);
        rsp->set_is_charging(false);
        rsp->set_emergency_stop(false);
        rsp->set_status_msg("OK");
        rsp->set_timestamp(
            static_cast<uint64_t>(ros::Time::now().toNSec() / 1000));
    }

    std::unique_ptr<ros::NodeHandle>    nh_;
    ros::Subscriber                     odomSub_;
    std::unique_ptr<ros::AsyncSpinner>  spinner_;

    std::mutex                   mutex_;
    CoreX::rpc::OdometryResponse lastOdom_;
    bool                         hasOdom_ = false;
};

// ============================================================================
// 插件导出函数（PluginLoader 通过 dlsym 查找）
// ============================================================================
extern "C" {

RpcServiceAdapter* createService()
{
    return new TelemetryServiceImpl();
}

void destroyService(RpcServiceAdapter* svc)
{
    delete svc;
}

}  // extern "C"
