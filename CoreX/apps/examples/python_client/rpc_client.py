#!/usr/bin/env python3
# ============================================================================
# CoreX RPC Python 客户端示例
#
# 用法:
#   python3 rpc_client.py --host 127.0.0.1 --port 8080 add --a 3 --b 5
#   python3 rpc_client.py --host 127.0.0.1 --port 8080 sub --a 10 --b 3
# ============================================================================

import socket
import struct
import argparse
import time

# ============================================================================
# CoreX RPC 线路协议常量
# ============================================================================
RPC_MAGIC = 0x42414E41          # "BANA" 魔数
HEADER_SIZE = 8                  # 4 字节魔数 + 4 字节长度
MESSAGE_TYPE_REQUEST = 0
MESSAGE_TYPE_RESPONSE = 1
MESSAGE_TYPE_ERROR = 2


def build_rpc_message(service, method, payload, msg_id=None, robot_id=""):
    """
    手动构建 RpcMessage protobuf 的二进数据。
    支持可选的 robot_id 字段（field 10）。
    """
    import struct as _struct

    if msg_id is None:
        msg_id = int(time.time() * 1000000) & 0xFFFFFFFFFFFFFFFF

    result = b""

    # Field 1: type (varint, value 0 = REQUEST)
    result += _varint_encode((1 << 3) | 0) + _varint_encode(MESSAGE_TYPE_REQUEST)

    # Field 2: id (fixed64)
    result += _struct.pack("<BQ", (2 << 3) | 1, msg_id)

    # Field 3: service (string)
    svc_bytes = service.encode('utf-8')
    result += _varint_encode((3 << 3) | 2) + _varint_encode(len(svc_bytes)) + svc_bytes

    # Field 4: method (string)
    method_bytes = method.encode('utf-8')
    result += _varint_encode((4 << 3) | 2) + _varint_encode(len(method_bytes)) + method_bytes

    # Field 5: payload (bytes)
    if isinstance(payload, str):
        payload = payload.encode('utf-8')
    result += _varint_encode((5 << 3) | 2) + _varint_encode(len(payload)) + payload

    # Field 10: robot_id (string) — ★ 新增
    if robot_id:
        robot_bytes = robot_id.encode('utf-8')
        result += _varint_encode((10 << 3) | 2) + _varint_encode(len(robot_bytes)) + robot_bytes

    return result, msg_id


def _varint_encode(value):
    """编码 varint"""
    bits = value & 0x7F
    value >>= 7
    result = bytearray()
    while value:
        result.append(bits | 0x80)
        bits = value & 0x7F
        value >>= 7
    result.append(bits)
    return bytes(result)


def build_math_request(method_name, a, b):
    """
    手动构建 MathRequest protobuf。

    MathRequest 字段:
      field 1: method_name (string)
      field 2: a (int32)
      field 3: b (int32)
    """
    result = b""

    # Field 1: method_name (string)
    name_bytes = method_name.encode('utf-8')
    result += _varint_encode((1 << 3) | 2)
    result += _varint_encode(len(name_bytes))
    result += name_bytes

    # Field 2: a (int32)
    result += _varint_encode((2 << 3) | 0)
    result += _varint_encode(a & 0xFFFFFFFF)

    # Field 3: b (int32)
    result += _varint_encode((3 << 3) | 0)
    result += _varint_encode(b & 0xFFFFFFFF)

    return result


def build_packet(payload_bytes):
    """构建 TLV 包：8 字节头 + 负载"""
    length = len(payload_bytes)
    header = struct.pack('!II', RPC_MAGIC, length)  # 大端序
    return header + payload_bytes


def parse_response_packet(data):
    """
    简易解析 RpcMessage 响应。
    返回 dict: {type, id, payload, error}
    """
    result = {'type': -1, 'id': 0, 'payload': b'', 'error': 0}
    pos = 0

    while pos < len(data):
        if pos >= len(data):
            break
        tag_byte = data[pos]
        pos += 1

        field_number = tag_byte >> 3
        wire_type = tag_byte & 0x07

        if wire_type == 0:  # varint
            value, pos = _varint_decode(data, pos)
            if field_number == 1:
                result['type'] = value
            elif field_number == 6:
                result['error'] = value
        elif wire_type == 1:  # fixed64
            if pos + 8 <= len(data):
                value = struct.unpack('<Q', data[pos:pos+8])[0]
                pos += 8
                if field_number == 2:
                    result['id'] = value
        elif wire_type == 2:  # length-delimited
            length, pos = _varint_decode(data, pos)
            if pos + length <= len(data):
                value = data[pos:pos+length]
                pos += length
                if field_number == 5:
                    result['payload'] = value
        else:
            break

    return result


def _varint_decode(data, pos):
    """解码 varint，返回 (value, new_position)"""
    value = 0
    shift = 0
    while pos < len(data):
        byte = data[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if not (byte & 0x80):
            break
        shift += 7
    return value, pos


def build_velocity_command(linear_x, linear_y, linear_z,
                           angular_x, angular_y, angular_z):
    """
    手动构建 VelocityCommand protobuf。

    VelocityCommand 字段:
      field 1: linear_x  (double)
      field 2: linear_y  (double)
      field 3: linear_z  (double)
      field 4: angular_x (double)
      field 5: angular_y (double)
      field 6: angular_z (double)
    """
    import struct as _struct

    result = b""

    # Field 1: linear_x (double, wire_type=1 → fixed64, little-endian)
    result += _struct.pack("<Bd", (1 << 3) | 1, linear_x)
    # Field 2: linear_y
    result += _struct.pack("<Bd", (2 << 3) | 1, linear_y)
    # Field 3: linear_z
    result += _struct.pack("<Bd", (3 << 3) | 1, linear_z)
    # Field 4: angular_x
    result += _struct.pack("<Bd", (4 << 3) | 1, angular_x)
    # Field 5: angular_y
    result += _struct.pack("<Bd", (5 << 3) | 1, angular_y)
    # Field 6: angular_z
    result += _struct.pack("<Bd", (6 << 3) | 1, angular_z)

    return result


def parse_control_response(payload):
    """
    简易解析 ControlResponse。
    ControlResponse 字段:
      field 1: success   (bool, varint)
      field 2: error_msg (string)
    """
    result = {'success': False, 'error_msg': ''}
    pos = 0

    while pos < len(payload):
        if pos >= len(payload):
            break
        tag_byte = payload[pos]
        pos += 1

        field_number = tag_byte >> 3
        wire_type = tag_byte & 0x07

        if wire_type == 0:  # varint
            value, pos = _varint_decode(payload, pos)
            if field_number == 1:
                result['success'] = bool(value)
        elif wire_type == 2:  # length-delimited
            length, pos = _varint_decode(payload, pos)
            if pos + length <= len(payload):
                value = payload[pos:pos+length]
                pos += length
                if field_number == 2:
                    result['error_msg'] = value.decode('utf-8', errors='replace')
        else:
            break

    return result


def build_generic_command(action, params):
    """
    手动构建 GenericCommand protobuf。

    GenericCommand 字段:
      field 1: action  (string)
      field 2: params  (bytes, JSON)
    """
    import json

    result = b""

    # Field 1: action (string)
    action_bytes = action.encode('utf-8')
    result += _varint_encode((1 << 3) | 2) + _varint_encode(len(action_bytes)) + action_bytes

    # Field 2: params (bytes, JSON)
    params_json = json.dumps(params).encode('utf-8')
    result += _varint_encode((2 << 3) | 2) + _varint_encode(len(params_json)) + params_json

    return result


def parse_generic_response(payload):
    """
    简易解析 GenericResponse。
    GenericResponse 字段:
      field 1: success   (bool, varint)
      field 2: message   (string)
      field 3: data      (bytes)
      field 4: error_msg (string)
    """
    result = {'success': False, 'message': '', 'data': b'', 'error_msg': ''}
    pos = 0

    while pos < len(payload):
        if pos >= len(payload):
            break
        tag_byte = payload[pos]
        pos += 1

        field_number = tag_byte >> 3
        wire_type = tag_byte & 0x07

        if wire_type == 0:  # varint
            value, pos = _varint_decode(payload, pos)
            if field_number == 1:
                result['success'] = bool(value)
        elif wire_type == 2:  # length-delimited
            length, pos = _varint_decode(payload, pos)
            if pos + length <= len(payload):
                value = payload[pos:pos+length]
                pos += length
                if field_number == 2:
                    result['message'] = value.decode('utf-8', errors='replace')
                elif field_number == 3:
                    result['data'] = value
                elif field_number == 4:
                    result['error_msg'] = value.decode('utf-8', errors='replace')
        else:
            break

    return result


def parse_math_response(payload):
    """
    简易解析 MathResponse。
    MathResponse 字段:
      field 1: result (int32)
      field 2: success (bool)
      field 3: error_msg (string)
    """
    result = {'result': 0, 'success': False, 'error_msg': ''}
    pos = 0

    while pos < len(payload):
        if pos >= len(payload):
            break
        tag_byte = payload[pos]
        pos += 1

        field_number = tag_byte >> 3
        wire_type = tag_byte & 0x07

        if wire_type == 0:  # varint
            value, pos = _varint_decode(payload, pos)
            if field_number == 1:
                result['result'] = value
            elif field_number == 2:
                result['success'] = bool(value)
        elif wire_type == 2:  # length-delimited
            length, pos = _varint_decode(payload, pos)
            if pos + length <= len(payload):
                value = payload[pos:pos+length]
                pos += length
                if field_number == 3:
                    result['error_msg'] = value.decode('utf-8', errors='replace')
        else:
            break

    return result


def rpc_call(host, port, service, method, request_payload, timeout=5.0, robot_id=""):
    """执行一次 RPC 调用，返回 raw response dict。可选 robot_id"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)

    try:
        sock.connect((host, port))

        # 构建并发送请求（含 robot_id）
        rpc_msg, msg_id = build_rpc_message(service, method, request_payload,
                                              robot_id=robot_id)
        packet = build_packet(rpc_msg)
        sock.sendall(packet)

        # 接收响应头（8 字节）
        header = b''
        while len(header) < HEADER_SIZE:
            chunk = sock.recv(HEADER_SIZE - len(header))
            if not chunk:
                raise ConnectionError("Connection closed before header received")
            header += chunk

        magic, length = struct.unpack('!II', header)

        if magic != RPC_MAGIC:
            raise ValueError(f"Bad magic number: 0x{magic:08X}, expected 0x{RPC_MAGIC:08X}")

        if length > 64 * 1024 * 1024:
            raise ValueError(f"Response too large: {length} bytes")

        # 接收响应体
        body = b''
        while len(body) < length:
            chunk = sock.recv(length - len(body))
            if not chunk:
                raise ConnectionError("Connection closed before body received")
            body += chunk

        # 解析响应
        rpc_response = parse_response_packet(body)

        if rpc_response['type'] == MESSAGE_TYPE_ERROR:
            return {'success': False, 'error': f"RPC Error code={rpc_response['error']}"}

        # 返回原始 payload 和 id，由调用方解析
        return {'raw_payload': rpc_response['payload'], 'id': rpc_response['id']}

    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(
        description='CoreX RPC Client — MathService 示例客户端',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s --host 127.0.0.1 --port 8080 add --a 3 --b 5
  %(prog)s --host 127.0.0.1 --port 8080 sub --a 10 --b 3
        """
    )

    parser.add_argument('--host', default='127.0.0.1', help='服务器地址 (默认: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=8080, help='服务器端口 (默认: 8080)')
    parser.add_argument('--timeout', type=float, default=5.0, help='超时秒数 (默认: 5.0)')

    subparsers = parser.add_subparsers(dest='command', help='RPC 方法')

    # add 子命令
    add_parser = subparsers.add_parser('add', help='调用 Add(a, b)')
    add_parser.add_argument('--a', type=int, required=True, help='第一个加数')
    add_parser.add_argument('--b', type=int, required=True, help='第二个加数')

    # sub 子命令
    sub_parser = subparsers.add_parser('sub', help='调用 Sub(a, b)')
    sub_parser.add_argument('--a', type=int, required=True, help='被减数')
    sub_parser.add_argument('--b', type=int, required=True, help='减数')

    # motion 子命令 — 发送 SetVelocity 指令到 ROS Bridge
    motion_parser = subparsers.add_parser('motion', help='调用 SetVelocity 发送运动控制指令')
    motion_parser.add_argument('--linear_x',  type=float, default=0.0, help='前进速度 m/s (默认 0)')
    motion_parser.add_argument('--linear_y',  type=float, default=0.0, help='横向速度 m/s (默认 0)')
    motion_parser.add_argument('--linear_z',  type=float, default=0.0, help='垂向速度 m/s (默认 0)')
    motion_parser.add_argument('--angular_x', type=float, default=0.0, help='绕x轴角速度 rad/s (默认 0)')
    motion_parser.add_argument('--angular_y', type=float, default=0.0, help='绕y轴角速度 rad/s (默认 0)')
    motion_parser.add_argument('--angular_z', type=float, default=0.0, help='绕z轴角速度 rad/s (默认 0)')
    motion_parser.add_argument('--mux', action='store_true',
                               help='发送到 /cmd_vel_mux/external (默认发送到 /cmd_vel)')

    # action 子命令 — 发送通用动作到 GenericActionBridge
    action_parser = subparsers.add_parser('action',
        help='调用 GenericAction.Execute 发送通用动作')
    action_parser.add_argument('action', help='动作名 (如 move, stop)')
    action_parser.add_argument('params', nargs='*',
                               help='key=value 参数, 如 linear_x=0.5 angular_z=0.2')
    action_parser.add_argument('--robot-id', default='',
                               help='目标机器人 ID')
    action_parser.add_argument('--service', default='CoreX.rpc.GenericAction',
                               help='RPC Service 名 (默认 CoreX.rpc.GenericAction)')
    action_parser.add_argument('--method', default='Execute',
                               help='RPC Method 名 (默认 Execute)')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    if args.command == 'add' or args.command == 'sub':
        # 构建请求负载
        method = args.command.capitalize()  # "add" → "Add", "sub" → "Sub"
        request_payload = build_math_request(method, args.a, args.b)

        print(f"Calling {method}(a={args.a}, b={args.b}) on {args.host}:{args.port}...")

        start_time = time.time()

        try:
            response = rpc_call(
                args.host, args.port,
                "CoreX.rpc.MathService",
                method,
                request_payload,
                timeout=args.timeout
            )

            elapsed_ms = (time.time() - start_time) * 1000

            if response.get('success') is False and 'error' in response:
                print(f"  Error: {response.get('error')}")
                return

            math_response = parse_math_response(response.get('raw_payload', b''))
            if math_response.get('success'):
                print(f"  Result: {math_response['result']}")
                print(f"  Latency: {elapsed_ms:.2f} ms")
            else:
                print(f"  Error: {math_response.get('error_msg', 'Unknown error')}")

        except socket.timeout:
            print(f"  Error: Request timed out ({args.timeout}s)")
        except ConnectionRefusedError:
            print(f"  Error: Connection refused — is CoreXDaemon running on {args.host}:{args.port}?")
        except Exception as e:
            print(f"  Error: {e}")

    elif args.command == 'motion':
        # ★ 修改：motion 命令现在通过 GenericAction 接口发送
        # 不再直接构造 VelocityCommand protobuf（格式不兼容 ROS Twist）
        # 而是使用 GenericActionBridge → RosMessageSerializer 正确转换
        import json

        if args.mux:
            action = "move_mux"
            ros_topic = "/cmd_vel_mux/external"
        else:
            action = "move"
            ros_topic = "/cmd_vel"

        # 构建 JSON 参数（只传非零值，零值由 YAML defaults 填充）
        params = {}
        if args.linear_x != 0.0: params['linear_x'] = args.linear_x
        if args.linear_y != 0.0: params['linear_y'] = args.linear_y
        if args.linear_z != 0.0: params['linear_z'] = args.linear_z
        if args.angular_x != 0.0: params['angular_x'] = args.angular_x
        if args.angular_y != 0.0: params['angular_y'] = args.angular_y
        if args.angular_z != 0.0: params['angular_z'] = args.angular_z
        # 如果全部为零，至少传一个 linear_x=0 以触发完整序列化
        if not params:
            params['linear_x'] = 0.0

        request_payload = build_generic_command(action, params)

        print(f"Calling CoreX.rpc.GenericAction.Execute → {ros_topic}")
        print(f"  Action: '{action}'")
        print(f"  linear:  x={args.linear_x}, y={args.linear_y}, z={args.linear_z}")
        print(f"  angular: x={args.angular_x}, y={args.angular_y}, z={args.angular_z}")
        print(f"  Params: {json.dumps(params)}")
        print(f"  Target: {args.host}:{args.port}...")

        start_time = time.time()

        try:
            response = rpc_call(
                args.host, args.port,
                "CoreX.rpc.GenericAction",
                "Execute",
                request_payload,
                timeout=args.timeout
            )

            elapsed_ms = (time.time() - start_time) * 1000

            if response.get('success') is False and 'error' in response:
                print(f"  Error: {response.get('error')}")
                return

            generic_resp = parse_generic_response(
                response.get('raw_payload', b''))
            if generic_resp.get('success'):
                msg = generic_resp.get('message', '')
                print(f"  OK — published to {ros_topic}")
                if msg:
                    print(f"  Message: {msg}")
                print(f"  Latency: {elapsed_ms:.2f} ms")
            else:
                print(f"  Server Error: {generic_resp.get('error_msg', 'Unknown')}")

        except socket.timeout:
            print(f"  Error: Request timed out ({args.timeout}s)")
        except ConnectionRefusedError:
            print(f"  Error: Connection refused — is CoreXDaemon running on {args.host}:{args.port}?")
        except Exception as e:
            print(f"  Error: {e}")

    elif args.command == 'action':
        # 解析 key=value 参数为 dict
        import json
        params = {}
        for kv in args.params:
            if '=' in kv:
                k, v = kv.split('=', 1)
                try:
                    params[k] = float(v) if '.' in v or 'e' in v.lower() else int(v)
                except ValueError:
                    params[k] = v

        request_payload = build_generic_command(args.action, params)

        print(f"Calling {args.service}.{args.method}")
        print(f"  Action: '{args.action}'")
        print(f"  Params: {json.dumps(params)}")
        print(f"  Robot ID: {args.robot_id or '(any)'}")
        print(f"  Target: {args.host}:{args.port}...")

        start_time = time.time()

        try:
            response = rpc_call(
                args.host, args.port,
                args.service, args.method,
                request_payload,
                timeout=args.timeout,
                robot_id=args.robot_id
            )

            elapsed_ms = (time.time() - start_time) * 1000

            if response.get('success') is False and 'error' in response:
                print(f"  Error: {response.get('error')}")
                return

            generic_resp = parse_generic_response(
                response.get('raw_payload', b''))
            if generic_resp.get('success'):
                msg = generic_resp.get('message', '')
                print(f"  OK: {msg}")
                print(f"  Latency: {elapsed_ms:.2f} ms")
            else:
                print(f"  Error: {generic_resp.get('error_msg', 'Unknown')}")

        except socket.timeout:
            print(f"  Error: Request timed out ({args.timeout}s)")
        except ConnectionRefusedError:
            print(f"  Error: Connection refused — is CoreXDaemon running on "
                  f"{args.host}:{args.port}?")
        except Exception as e:
            print(f"  Error: {e}")

    else:
        parser.print_help()


if __name__ == '__main__':
    main()
