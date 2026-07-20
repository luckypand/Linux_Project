#!/usr/bin/env python3
# ============================================================================
# fake_sensors.py — ROS 端假传感器发布器（在机器人/ROS 机器上运行）
#
# 没有真实深度相机/雷达时，用它制造测试数据：
#   /camera/depth/image_raw  sensor_msgs/Image      320x240 16UC1, 10Hz
#                            深度值随时间正弦变化（1000±500mm），便于观察数据在动
#   /points                  sensor_msgs/PointCloud2 一圈缓慢旋转的半径 2m 圆形"墙"
#   /odom                    nav_msgs/Odometry       沿圆周运动的假里程计, 20Hz
#
# 用法（ROS Noetic）:
#   source /opt/ros/noetic/setup.bash
#   roscore &
#   python3 fake_sensors.py
#
# 验证发布正常:
#   rostopic hz /camera/depth/image_raw /points /odom
# ============================================================================
import math
import struct

import rospy
from std_msgs.msg import Header
from sensor_msgs.msg import Image, PointCloud2
from nav_msgs.msg import Odometry
import sensor_msgs.point_cloud2 as pc2


def main():
    rospy.init_node('fake_sensors')
    img_pub = rospy.Publisher('/camera/depth/image_raw', Image, queue_size=2)
    pc_pub = rospy.Publisher('/points', PointCloud2, queue_size=2)
    odom_pub = rospy.Publisher('/odom', Odometry, queue_size=10)

    rate = rospy.Rate(20)          # 主循环 20Hz；图像/点云每 2 拍发一次 (10Hz)
    W, H = 320, 240                # 320x240 16UC1 = 150KB/帧，SHM 10MB 槽放得下
    t = 0.0
    tick = 0

    rospy.loginfo("fake_sensors: publishing /camera/depth/image_raw /points /odom")

    while not rospy.is_shutdown():
        now = rospy.Time.now()
        t += 0.05
        tick += 1

        # ---- /odom：沿半径 3m 圆周运动 ----
        odom = Odometry()
        odom.header = Header(stamp=now, frame_id='odom')
        odom.child_frame_id = 'base_link'
        odom.pose.pose.position.x = 3.0 * math.cos(t * 0.2)
        odom.pose.pose.position.y = 3.0 * math.sin(t * 0.2)
        odom.pose.pose.orientation.w = 1.0
        odom.twist.twist.linear.x = 0.6
        odom.twist.twist.angular.z = 0.2
        odom_pub.publish(odom)

        if tick % 2 == 0:
            # ---- 深度图：整幅图深度值 1000±500mm 正弦波动 ----
            depth_mm = int(1000 + 500 * math.sin(t))
            img = Image()
            img.header = Header(stamp=now, frame_id='camera_depth')
            img.height, img.width = H, W
            img.encoding = '16UC1'             # 深度相机常见编码（毫米）
            img.is_bigendian = 0
            img.step = W * 2
            img.data = struct.pack('<H', depth_mm) * (W * H)
            img_pub.publish(img)

            # ---- 点云：半径 2m、缓慢旋转的圆形"墙" ----
            pts = [(2.0 * math.cos(a * 0.0628 + t * 0.1),
                    2.0 * math.sin(a * 0.0628 + t * 0.1),
                    0.0) for a in range(100)]
            cloud = pc2.create_cloud_xyz32(Header(stamp=now, frame_id='laser'), pts)
            pc_pub.publish(cloud)

        rate.sleep()


if __name__ == '__main__':
    main()
