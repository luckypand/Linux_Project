#pragma once
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

// ============================================================================
// ConfigManager —— YAML 配置文件解析器
//
// 配置文件格式 (YAML)：
//   server:
//     host: "0.0.0.0"
//     port: 8080
//     worker_threads: 4
//
// 使用方式：
//   ConfigManager cfg;
//   cfg.load("corex_daemon.yaml");
//   std::string host = cfg.getString("server.host", "127.0.0.1");
//   int port = cfg.getInt("server.port", 8080);
// ============================================================================
class ConfigManager
{
public:
    ConfigManager() = default;

    // 从 YAML 文件加载配置，失败返回 false，错误信息写入 errMsg
    bool load(const std::string& filepath);
    const std::string& getLastError() const { return lastError_; }

    // 类型安全的读取方法，路径不存在时返回默认值
    // 路径格式：用 '.' 分隔嵌套层级，如 "server.host", "ipc.enabled"
    std::string getString(const std::string& path, const std::string& defaultValue = "") const;
    int         getInt(const std::string& path, int defaultValue = 0) const;
    bool        getBool(const std::string& path, bool defaultValue = false) const;
    double      getDouble(const std::string& path, double defaultValue = 0.0) const;

    // 获取字符串列表（如 plugins.autoload_list）
    std::vector<std::string> getStringList(const std::string& path) const;

    // 获取原始 YAML::Node（用于复杂结构）
    YAML::Node getNode(const std::string& path) const;

    // 检查路径是否存在
    bool hasPath(const std::string& path) const;

private:
    // 遍历路径 "a.b.c" → 找到对应的 YAML::Node
    YAML::Node resolvePath(const std::string& path) const;

    YAML::Node  root_;
    std::string lastError_;
};
