#!/usr/bin/env python3
# ============================================================================
# robot_rpc.py — CoreX ROS Bridge 端到端测试客户端（云端侧，零依赖）
#
# 与 apps/examples/python_client/rpc_client.py 同源：手写 Protobuf Wire Format
# 编码 RpcMessage 信封 + TLV 帧。
#
# ★ 关键认识：TopicBridge 是【字节透传】（见 src/ros_bridge/TopicBridge.cpp）——
#   - publish 方向：RPC payload 被原样当作 ROS 序列化字节发布到 topic
#   - subscribe 方向：缓存并回传的就是 ROS 序列化的原始字节
#   所以本客户端的 payload 使用 ROS 序列化格式（小端），而不是 protobuf。
#   只有外层 RpcMessage 信封是 protobuf。
#
# 用法:
#   python3 robot_rpc.py --host <robot_ip> vel --linear 0.5 --angular 0.2   # 测试一
#   python3 robot_rpc.py --host <robot_ip> odom                             # 测试二
#   python3 robot_rpc.py --host <robot_ip> odom --loop 20                   # 20Hz 轮询
#   python3 robot_rpc.py --host <robot_ip> image                            # 测试三
#   python3 robot_rpc.py --host <robot_ip> cloud                            # 测试三
#
# 调试开关:
#   --skip4     解析回传数据时跳过开头 4 字节。若 frame_id 显示乱码，
#               说明缓存字节带了 TCPROS 的 4 字节长度前缀，开这个开关。
#   --lenprefix vel 下发时在 payload 前加 4 字节长度前缀。若 rostopic echo
#               的数值错乱或订阅端报错，开这个开关（roscpp 的
#               SerializedMessage 线上格式是 [len][body]）。
# ============================================================================
import socket
import struct
import argparse
import time

RPC_MAGIC = 0x42414E41   # "BANA"


# ============================================================================
# 第一层：RpcMessage 信封（protobuf wire format 手写编解码）
# ============================================================================
def _varint(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        out.append(b | (0x80 if v else 0))
        if not v:
            return bytes(out)


def _varint_dec(b, p):
    v = s = 0
    while p < len(b):
        c = b[p]
        p += 1
        v |= (c & 0x7F) << s
        if not (c & 0x80):
            break
        s += 7
    return v, p


def build_envelope(service, method, payload):
    """RpcMessage{type=REQUEST, id, service, method, payload}"""
    msg_id = int(time.time() * 1e6) & 0xFFFFFFFFFFFFFFFF
    m = _varint((1 << 3) | 0) + _varint(0)                      # field1 type=0
    m += struct.pack("<BQ", (2 << 3) | 1, msg_id)               # field2 id fixed64
    m += _varint((3 << 3) | 2) + _varint(len(service)) + service.encode()
    m += _varint((4 << 3) | 2) + _varint(len(method)) + method.encode()
    m += _varint((5 << 3) | 2) + _varint(len(payload)) + payload
    return m


def parse_envelope(b):
    r = {'type': -1, 'payload': b'', 'error': 0}
    p = 0
    while p < len(b):
        tag = b[p]
        p += 1
        f, w = tag >> 3, tag & 7
        if w == 0:                       # varint
            v, p = _varint_dec(b, p)
            if f == 1:
                r['type'] = v
            elif f == 6:
                r['error'] = v
        elif w == 1:                     # fixed64
            p += 8
        elif w == 2:                     # length-delimited
            n, p = _varint_dec(b, p)
            if f == 5:
                r['payload'] = b[p:p + n]
            p += n
        else:
            break
    return r


def _recvn(s, n):
    buf = b''
    while len(buf) < n:
        c = s.recv(n - len(buf))
        if not c:
            raise ConnectionError("connection closed")
        buf += c
    return buf


def rpc_call(host, port, service, method, payload=b'', timeout=5.0):
    """一次完整调用：TLV 帧 → 信封 → 发送 → 收响应 → 解信封"""
    with socket.create_connection((host, port), timeout=timeout) as s:
        env = build_envelope(service, method, payload)
        s.sendall(struct.pack('!II', RPC_MAGIC, len(env)) + env)   # 8 字节大端 TLV 头
        magic, length = struct.unpack('!II', _recvn(s, 8))
        if magic != RPC_MAGIC:
            raise ValueError(f"bad magic 0x{magic:08X}")
        return parse_envelope(_recvn(s, length))


# ============================================================================
# 第二层：ROS 序列化格式（小端）编解码
# ============================================================================
def parse_ros_string(b, p):
    n = struct.unpack_from('<I', b, p)[0]
    p += 4
    return b[p:p + n].decode(errors='replace'), p + n


def parse_ros_header(b, p):
    """std_msgs/Header = seq(u32) + stamp(secs u32, nsecs u32) + frame_id(string)"""
    seq, secs, nsecs = struct.unpack_from('<3I', b, p)
    p += 12
    frame_id, p = parse_ros_string(b, p)
    return {'seq': seq, 'stamp': secs + nsecs * 1e-9, 'frame_id': frame_id}, p


def ser_twist(lx, az):
    """geometry_msgs/Twist = linear(x,y,z) + angular(x,y,z)，6×float64 = 48 字节"""
    return struct.pack('<6d', lx, 0.0, 0.0, 0.0, 0.0, az)


def parse_odometry(b, skip=0):
    """nav_msgs/Odometry = Header + child_frame_id + pose(7d)+cov(36d) + twist(6d)+cov(36d)"""
    p = skip
    hdr, p = parse_ros_header(b, p)
    child, p = parse_ros_string(b, p)
    px, py, pz, ox, oy, oz, ow = struct.unpack_from('<7d', b, p)
    p += 56
    p += 36 * 8                                   # pose covariance
    vx, vy, vz, wx, wy, wz = struct.unpack_from('<6d', b, p)
    return {'header': hdr, 'child_frame_id': child,
            'pos': (px, py, pz), 'orient': (ox, oy, oz, ow),
            'linear': (vx, vy, vz), 'angular': (wx, wy, wz)}


def parse_image(b, skip=0):
    """sensor_msgs/Image = Header + h,w(u32) + encoding(str) + bigendian(u8) + step(u32) + data"""
    p = skip
    hdr, p = parse_ros_header(b, p)
    h, w = struct.unpack_from('<2I', b, p)
    p += 8
    enc, p = parse_ros_string(b, p)
    p += 1                                        # is_bigendian
    step = struct.unpack_from('<I', b, p)[0]
    p += 4
    n = struct.unpack_from('<I', b, p)[0]
    p += 4
    return {'header': hdr, 'height': h, 'width': w, 'encoding': enc,
            'step': step, 'data_len': n, 'data': b[p:p + n]}


def parse_pointcloud2(b, skip=0):
    """sensor_msgs/PointCloud2 = Header + h,w + fields[] + bigendian + point_step + row_step + data + is_dense"""
    p = skip
    hdr, p = parse_ros_header(b, p)
    h, w = struct.unpack_from('<2I', b, p)
    p += 8
    nf = struct.unpack_from('<I', b, p)[0]
    p += 4
    fields = []
    for _ in range(nf):
        name, p = parse_ros_string(b, p)
        off, dt, cnt = struct.unpack_from('<IBI', b, p)   # PointField{offset,datatype,count}
        p += 9
        fields.append({'name': name, 'offset': off, 'datatype': dt})
    p += 1                                        # is_bigendian
    pstep, rstep = struct.unpack_from('<2I', b, p)
    p += 8
    n = struct.unpack_from('<I', b, p)[0]
    p += 4
    return {'header': hdr, 'height': h, 'width': w, 'fields': fields,
            'point_step': pstep, 'row_step': rstep, 'data': b[p:p + n]}


# ============================================================================
# 子命令
# ============================================================================
def check_resp(resp):
    if resp['type'] == 2:
        print(f"  ✗ RPC 错误响应, error_code={resp['error']}"
              f"（服务名/方法名与 YAML 不一致？Bridge 未启用？）")
        return None
    if not resp['payload']:
        print("  ✗ 空响应 — Bridge 尚无该 topic 的缓存数据（数据源未发布 / topic 名不匹配）")
        return None
    return resp['payload']


def cmd_vel(args):
    payload = ser_twist(args.linear, args.angular)
    if args.lenprefix:
        payload = struct.pack('<I', len(payload)) + payload
    t0 = time.time()
    resp = rpc_call(args.host, args.port, "MotionControl", "SetVelocity", payload)
    ms = (time.time() - t0) * 1000
    if resp['type'] == 2:
        print(f"  ✗ RPC 错误, code={resp['error']}")
    else:
        print(f"  ✓ SetVelocity(linear={args.linear}, angular={args.angular}) → "
              f"'{resp['payload'].decode(errors='replace')}'  {ms:.2f} ms")
        print(f"    在机器人端验证: rostopic echo /cmd_vel")


def cmd_odom(args):
    t0 = time.time()
    resp = rpc_call(args.host, args.port, "RobotTelemetry", "GetOdometry")
    ms = (time.time() - t0) * 1000
    payload = check_resp(resp)
    if payload is None:
        return
    o = parse_odometry(payload, 4 if args.skip4 else 0)
    print(f"  ✓ Odometry ({len(payload)}B, {ms:.2f} ms)  frame={o['header']['frame_id']}"
          f" → {o['child_frame_id']}")
    print(f"    位置   x={o['pos'][0]:.3f}  y={o['pos'][1]:.3f}  z={o['pos'][2]:.3f}")
    print(f"    姿态   ({o['orient'][0]:.3f}, {o['orient'][1]:.3f},"
          f" {o['orient'][2]:.3f}, {o['orient'][3]:.3f})")
    print(f"    线速度 x={o['linear'][0]:.3f}  角速度 z={o['angular'][2]:.3f}")


def cmd_image(args):
    t0 = time.time()
    resp = rpc_call(args.host, args.port, "DepthCamera", "GetImage")
    ms = (time.time() - t0) * 1000
    payload = check_resp(resp)
    if payload is None:
        return
    img = parse_image(payload, 4 if args.skip4 else 0)
    mid = img['data'][len(img['data']) // 2: len(img['data']) // 2 + 2]
    center = struct.unpack('<H', mid)[0] if img['encoding'] == '16UC1' and len(mid) == 2 else '?'
    print(f"  ✓ Image {img['width']}x{img['height']} enc={img['encoding']}"
          f" step={img['step']} data={img['data_len']}B  ({ms:.2f} ms)")
    print(f"    frame={img['header']['frame_id']} stamp={img['header']['stamp']:.3f}"
          f" 中心深度={center}mm")


def cmd_cloud(args):
    t0 = time.time()
    resp = rpc_call(args.host, args.port, "Lidar", "GetPointCloud")
    ms = (time.time() - t0) * 1000
    payload = check_resp(resp)
    if payload is None:
        return
    pc = parse_pointcloud2(payload, 4 if args.skip4 else 0)
    npts = len(pc['data']) // pc['point_step'] if pc['point_step'] else 0
    offs = {f['name']: f['offset'] for f in pc['fields']}
    print(f"  ✓ PointCloud2 {pc['width']}x{pc['height']} 点数={npts}"
          f" point_step={pc['point_step']}  ({ms:.2f} ms)")
    print(f"    fields={[f['name'] for f in pc['fields']]}"
          f" frame={pc['header']['frame_id']}")
    for i in range(min(3, npts)):
        base = i * pc['point_step']
        x = struct.unpack_from('<f', pc['data'], base + offs.get('x', 0))[0]
        y = struct.unpack_from('<f', pc['data'], base + offs.get('y', 4))[0]
        z = struct.unpack_from('<f', pc['data'], base + offs.get('z', 8))[0]
        print(f"    p{i} = ({x:.3f}, {y:.3f}, {z:.3f})")


def main():
    ap = argparse.ArgumentParser(description='CoreX ROS Bridge 端到端测试客户端（云端侧）')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=8080)
    ap.add_argument('--skip4', action='store_true', help='解析时跳过开头 4 字节长度前缀')
    ap.add_argument('--lenprefix', action='store_true', help='下发时附加 4 字节长度前缀')
    ap.add_argument('--loop', type=float, default=0, help='轮询频率 Hz（0=单次）')
    sub = ap.add_subparsers(dest='cmd', required=True)

    v = sub.add_parser('vel', help='测试一：下发速度指令 → /cmd_vel')
    v.add_argument('--linear', type=float, default=0.0, help='线速度 m/s')
    v.add_argument('--angular', type=float, default=0.0, help='角速度 rad/s')
    sub.add_parser('odom', help='测试二：拉取 /odom 里程计并解析')
    sub.add_parser('image', help='测试三：拉取深度图并解析')
    sub.add_parser('cloud', help='测试三：拉取点云并解析')
    args = ap.parse_args()

    handler = {'vel': cmd_vel, 'odom': cmd_odom,
               'image': cmd_image, 'cloud': cmd_cloud}[args.cmd]
    try:
        if args.loop > 0:
            while True:
                handler(args)
                time.sleep(1.0 / args.loop)
        else:
            handler(args)
    except ConnectionRefusedError:
        print(f"  ✗ 连接被拒绝 — CoreXDaemon 是否运行在 {args.host}:{args.port}？")
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
