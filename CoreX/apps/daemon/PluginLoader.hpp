#pragma once
#include "../../src/rpc/RpcServiceAdapter.hpp"
#include <string>
#include <vector>
#include <memory>

// ============================================================================
// PluginLoader —— 动态库加载器
//
// 从指定目录加载 .so 插件文件，每个插件必须导出以下 C 函数：
//   extern "C" RpcServiceAdapter* createService();
//   extern "C" void destroyService(RpcServiceAdapter* svc);
//
// 使用方式：
//   PluginLoader loader;
//   std::vector<RpcServiceAdapter*> svcs = loader.loadDirectory("./plugins");
//   // ... 使用 services ...
//   loader.unloadAll();  // 或析构时自动卸载
// ============================================================================
class PluginLoader
{
public:
    struct PluginEntry {
        std::string         filepath;   // .so 文件路径
        void*               handle;     // dlopen 句柄
        RpcServiceAdapter*  service;    // 创建的服务实例
    };

    PluginLoader() = default;
    ~PluginLoader();

    // 禁止拷贝
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    // 从目录加载所有 .so 文件，返回创建的服务适配器列表
    std::vector<RpcServiceAdapter*> loadDirectory(const std::string& dirPath);

    // 加载单个 .so 文件
    RpcServiceAdapter* loadPlugin(const std::string& filepath);

    // 卸载所有已加载的插件
    void unloadAll();

    // 卸载单个插件
    bool unloadPlugin(const std::string& filepath);

    // 获取已加载的插件列表（只读）
    const std::vector<PluginEntry>& getPlugins() const { return plugins_; }

    // 获取加载过程中的最后一次错误信息
    const std::string& getLastError() const { return lastError_; }

private:
    std::vector<PluginEntry> plugins_;
    std::string              lastError_;
};
