// ============================================================================
// shm_reader.cpp — 本地 ShmTopicBus 直读订阅者（测试三，无需 ROS 环境）
//
// 数据流：ROS 传感器驱动 → TopicBridge::onRosMessage → ShmTopicBus::publish
//         → 共享内存环形缓冲 → eventfd 唤醒 → 本程序 tryRecv（< 10μs）
//
// 前提：CoreXDaemon 已用 corex_test.yaml 启动，且对应 topic 配置了
//       use_shm_topic: true + use_shm: true（10MB 槽位）。
//
// 编译（在本目录，无需 ROS）:
//   g++ -std=c++17 -O2 shm_reader.cpp \
//       ../../../src/ros_bridge/ShmTopicBus.cpp ../../../src/ipc/ShmSegment.cpp \
//       -I../../../src/ros_bridge -I../../../src/ipc \
//       -lpthread -lrt -o shm_reader
//   （若报日志/其他未定义符号，改为链接静态库: 追加 -L../../../build -lrpc -lprotobuf）
//
// 用法:
//   ./shm_reader /camera/depth/image_raw image
//   ./shm_reader /points cloud
// ============================================================================

#include "ShmTopicBus.hpp"

#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

// ---- ROS 序列化格式（小端）的最小解析工具 ----
static uint32_t rdU32(const std::string& s, size_t& p)
{
    uint32_t v;
    memcpy(&v, s.data() + p, 4);
    p += 4;
    return v;
}

static std::string rdStr(const std::string& s, size_t& p)
{
    uint32_t n = rdU32(s, p);
    std::string r = s.substr(p, n);
    p += n;
    return r;
}

// sensor_msgs/Image：Header + h,w + encoding + bigendian + step + data
static void printImage(const std::string& msg)
{
    size_t p = 0;
    uint32_t seq  = rdU32(msg, p);
    uint32_t secs = rdU32(msg, p);
    uint32_t nsec = rdU32(msg, p);
    std::string frame = rdStr(msg, p);
    uint32_t h = rdU32(msg, p);
    uint32_t w = rdU32(msg, p);
    std::string enc = rdStr(msg, p);
    p += 1;                                     // is_bigendian
    uint32_t step = rdU32(msg, p);
    uint32_t len  = rdU32(msg, p);

    // 取图像中心像素的深度值（16UC1 = 每像素 2 字节，单位 mm）
    uint16_t center = 0;
    if (enc == "16UC1" && len >= 2) {
        memcpy(&center, msg.data() + p + (len / 2 & ~1u), 2);
    }
    printf("[Image] seq=%u %ux%u enc=%s step=%u data=%uB 中心深度=%umm frame=%s t=%u.%03u\n",
           seq, w, h, enc.c_str(), step, len, center, frame.c_str(), secs, nsec / 1000000);
}

// sensor_msgs/PointCloud2：Header + h,w + fields[] + bigendian + steps + data
static void printCloud(const std::string& msg)
{
    size_t p = 0;
    uint32_t seq  = rdU32(msg, p);
    rdU32(msg, p);                              // secs
    rdU32(msg, p);                              // nsecs
    std::string frame = rdStr(msg, p);
    uint32_t h = rdU32(msg, p);
    uint32_t w = rdU32(msg, p);

    uint32_t nf = rdU32(msg, p);
    uint32_t xoff = 0, yoff = 4, zoff = 8;
    for (uint32_t i = 0; i < nf; i++) {
        std::string name = rdStr(msg, p);
        uint32_t off = rdU32(msg, p);
        p += 1;                                 // datatype (7 = FLOAT32)
        rdU32(msg, p);                          // count
        if (name == "x") xoff = off;
        else if (name == "y") yoff = off;
        else if (name == "z") zoff = off;
    }
    p += 1;                                     // is_bigendian
    uint32_t pstep = rdU32(msg, p);
    rdU32(msg, p);                              // row_step
    uint32_t len = rdU32(msg, p);

    uint32_t npts = pstep ? len / pstep : 0;
    float x = 0, y = 0, z = 0;
    if (npts) {
        memcpy(&x, msg.data() + p + xoff, 4);
        memcpy(&y, msg.data() + p + yoff, 4);
        memcpy(&z, msg.data() + p + zoff, 4);
    }
    printf("[Cloud] seq=%u %ux%u 点数=%u p0=(%.2f, %.2f, %.2f) frame=%s\n",
           seq, w, h, npts, x, y, z, frame.c_str());
}

int main(int argc, char* argv[])
{
    std::string topic = (argc > 1) ? argv[1] : "/camera/depth/image_raw";
    std::string kind  = (argc > 2) ? argv[2] : "image";

    // 槽大小必须与创建者(TopicBridge)一致：use_shm: true → 10MB（TopicBridge::start）
    ShmTopicBus bus(topic, 10 * 1024 * 1024, 16, /*isCreator=*/false);
    if (!bus.isValid()) {
        fprintf(stderr, "附加共享内存段失败 — CoreXDaemon 已启动、且 %s 配置了 use_shm_topic: true ?\n",
                topic.c_str());
        return 1;
    }

    int efd = bus.subscribe();
    if (efd < 0) {
        fprintf(stderr, "subscribe 失败（订阅者已满？单 topic 最多 8 个）\n");
        return 1;
    }
    printf("[shm_reader] 已附加 %s，等待数据 (Ctrl-C 退出)...\n", topic.c_str());

    std::string msg;
    while (true) {
        struct pollfd pfd{efd, POLLIN, 0};
        int rc = poll(&pfd, 1, 2000);
        if (rc <= 0) {
            printf("... 2s 内无新数据（数据源是否在发布？）\n");
            continue;
        }
        uint64_t cnt;
        (void)!read(efd, &cnt, sizeof(cnt));    // 清除 eventfd 计数

        while (bus.tryRecv(msg)) {
            if (kind == "image") printImage(msg);
            else                 printCloud(msg);
        }
    }
    return 0;
}
