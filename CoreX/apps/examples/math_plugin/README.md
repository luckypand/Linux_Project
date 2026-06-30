# Math Service Plugin

示例业务插件，为 CoreXDaemon 提供 Add/Sub 两个 RPC 方法。

## 编译

### 方式 1: 使用 build.sh（推荐）

```bash
cd /root/Cplus/CoreX
./build.sh math_plugin
```

### 方式 2: 手动编译

```bash
cd /root/Cplus/CoreX/apps/examples/math_plugin
mkdir build && cd build
cmake ..
make
```

编译产物：`libmath_service.so` → 输出到 `CoreX/build/plugins/`

## 部署

将 `libmath_service.so` 复制到 CoreXDaemon 的插件目录：

```bash
cp build/libmath_service.so /path/to/plugins/
```

或修改 `corex_daemon.yaml` 中的 `plugins.directory` 指向编译输出目录。

## 使用

启动 CoreXDaemon 后，服务会自动注册：

```
[CoreXDaemon] Registered service: CoreX.rpc.MathService
```

然后使用任何 RPC 客户端调用 Add/Sub 方法。
