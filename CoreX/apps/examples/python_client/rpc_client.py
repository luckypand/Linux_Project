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


def build_rpc_message(service, method, payload, msg_id=None):
    """
    手动构建 RpcMessage protobuf 的二进数据。

    由于我们不在 Python 端使用 protobuf 编译，这里手写最简单的
    protobuf wire format 编码方式，仅支持 string/bytes/fixed64/int32 字段。

    更正式的做法是用 protoc 生成 Python pb2 文件。
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


def rpc_call(host, port, service, method, request_payload, timeout=5.0):
    """执行一次 RPC 调用"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)

    try:
        sock.connect((host, port))

        # 构建并发送请求
        rpc_msg, msg_id = build_rpc_message(service, method, request_payload)
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

        math_response = parse_math_response(rpc_response['payload'])
        return math_response

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

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

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

        if response.get('success'):
            print(f"  Result: {response['result']}")
            print(f"  Latency: {elapsed_ms:.2f} ms")
        else:
            print(f"  Error: {response.get('error', 'Unknown error')}")
            print(f"  Error message: {response.get('error_msg', '')}")

    except socket.timeout:
        print(f"  Error: Request timed out ({args.timeout}s)")
    except ConnectionRefusedError:
        print(f"  Error: Connection refused — is CoreXDaemon running on {args.host}:{args.port}?")
    except Exception as e:
        print(f"  Error: {e}")


if __name__ == '__main__':
    main()
