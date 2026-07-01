// ============================================================================
// control_plugin.cpp — 机器人运动控制插件
//
// 功能：接收 RPC 运动控制指令，发布到 ROS /cmd_vel topic
//
// 部署到机器人：
//   编译为 libcontrol_plugin.so，放入 CoreXDaemon 的 plugins 目录
//   确保 ROS master 已启动（roscore）
//
// 编译依赖（需 ROS 环境）:
//   roscpp, geometry_msgs
// ============================================================================

#include "../../../src/rpc/RpcServiceAdapter.hpp"
#include "../../../proto/robot_service.pb.h"
#include <ros/ros.h>
#include <geometry_msgs/Twist.h>
#include <mutex>
#include <memory>
#include <cstring>

// 引用 telemetry_plugin 中的 ROS 初始化（链接时共享）
// 如果 telemetry_plugin 先加载，ROS 已初始化；否则这里自己初始化
static std::once_flag g_ros_init_flag_ctrl;

static void ensureRosInit()
{
    std::call_once(g_ros_init_flag_ctrl, []() {
        // 检查是否已被其他插件初始化
        if (!ros::isInitialized()) {
            int   argc = 1;
            char  arg0[] = "corex_robot_bridge";
            char* argv[] = {arg0, nullptr};

            ros::init(argc, argv, "corex_robot_bridge",
                      ros::init_options::NoSigintHandler);
            ROS_INFO("[CoreX] ROS initialized by control_plugin");
        }
    });
}

// ============================================================================
// MotionControlServiceImpl — 接收 RPC 指令 → 发布 ROS /cmd_vel
// ============================================================================
class MotionControlServiceImpl : public RpcServiceAdapter
{
public:
    MotionControlServiceImpl()
        : RpcServiceAdapter(
              CoreX::rpc::VelocityCommand::descriptor()
                  ->file()->FindServiceByName("MotionControl"))
    {
        ensureRosInit();

        // ---- 创建 ROS NodeHandle 和 /cmd_vel publisher ----
        nh_ = std::make_unique<ros::NodeHandle>("~");
        cmdVelPub_ = nh_->advertise<geometry_msgs::Twist>("/cmd_vel", 1);

        ROS_INFO("[ControlPlugin] /cmd_vel publisher ready, service registered");

        // ---- 注册 RPC 方法 ----
        registerHandler("SetVelocity",
            [this](::google::protobuf::Message* req,
                   ::google::protobuf::Message* resp)
            {
                auto* cmd = static_cast<CoreX::rpc::VelocityCommand*>(req);
                auto* rsp = static_cast<CoreX::rpc::ControlResponse*>(resp);
                this->publishVelocity(cmd, rsp);
            });

        registerHandler("Stop",
            [this](::google::protobuf::Message* /*req*/,
                   ::google::protobuf::Message* resp)
            {
                auto* rsp = static_cast<CoreX::rpc::ControlResponse*>(resp);
                this->publishStop(rsp);
            });
    }

    ~MotionControlServiceImpl() override
    {
        cmdVelPub_.shutdown();
    }

private:
    // ---- 发布速度指令 ----
    void publishVelocity(const CoreX::rpc::VelocityCommand* cmd,
                         CoreX::rpc::ControlResponse* rsp)
    {
        geometry_msgs::Twist twist;
        twist.linear.x  = cmd->linear_x();
        twist.linear.y  = cmd->linear_y();
        twist.linear.z  = cmd->linear_z();
        twist.angular.x = cmd->angular_x();
        twist.angular.y = cmd->angular_y();
        twist.angular.z = cmd->angular_z();

        cmdVelPub_.publish(twist);

        rsp->set_success(true);
        ROS_INFO("[ControlPlugin] SetVelocity: linear_x=%.2f angular_z=%.2f",
                 cmd->linear_x(), cmd->angular_z());
    }

    // ---- 发布急停指令（全部归零）----
    void publishStop(CoreX::rpc::ControlResponse* rsp)
    {
        geometry_msgs::Twist twist;  // 默认全零
        cmdVelPub_.publish(twist);

        rsp->set_success(true);
        ROS_INFO("[ControlPlugin] Stop: all velocities set to zero");
    }

    std::unique_ptr<ros::NodeHandle> nh_;
    ros::Publisher                    cmdVelPub_;
};

// ============================================================================
// 插件导出函数（PluginLoader 通过 dlsym 查找）
// ============================================================================
extern "C" {

RpcServiceAdapter* createService()
{
    return new MotionControlServiceImpl();
}

void destroyService(RpcServiceAdapter* svc)
{
    delete svc;
}

}  // extern "C"
