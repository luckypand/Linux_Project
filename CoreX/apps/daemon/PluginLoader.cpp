#include "PluginLoader.hpp"
#include <dlfcn.h>
#include <dirent.h>
#include <cstring>
#include <algorithm>

// 插件必须导出的工厂函数签名
using CreateServiceFunc  = RpcServiceAdapter* (*)();
using DestroyServiceFunc = void (*)(RpcServiceAdapter*);

PluginLoader::~PluginLoader()
{
    unloadAll();
}

RpcServiceAdapter* PluginLoader::loadPlugin(const std::string& filepath)
{
    // 清除之前的错误
    lastError_.clear();

    // 打开动态库
    void* handle = dlopen(filepath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        lastError_ = std::string("dlopen failed: ") + dlerror();
        return nullptr;
    }

    // 查找 createService 符号
    auto createFn = reinterpret_cast<CreateServiceFunc>(dlsym(handle, "createService"));
    if (!createFn) {
        lastError_ = std::string("dlsym(createService) failed: ") + dlerror();
        dlclose(handle);
        return nullptr;
    }

    // 查找 destroyService 符号（可选，不存在时用 delete）
    auto destroyFn = reinterpret_cast<DestroyServiceFunc>(dlsym(handle, "destroyService"));

    // 创建服务实例
    RpcServiceAdapter* service = createFn();
    if (!service) {
        lastError_ = "createService() returned nullptr";
        dlclose(handle);
        return nullptr;
    }

    // 记录插件信息
    PluginEntry entry;
    entry.filepath = filepath;
    entry.handle   = handle;
    entry.service  = service;
    plugins_.push_back(entry);

    return service;
}

std::vector<RpcServiceAdapter*> PluginLoader::loadDirectory(const std::string& dirPath)
{
    std::vector<RpcServiceAdapter*> services;

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        lastError_ = std::string("cannot open directory: ") + dirPath;
        return services;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);

        // 只加载 .so 文件
        if (name.size() < 3 || name.substr(name.size() - 3) != ".so") {
            continue;
        }

        // 跳过隐藏文件
        if (name[0] == '.') continue;

        std::string fullPath = dirPath + "/" + name;
        RpcServiceAdapter* svc = loadPlugin(fullPath);
        if (svc) {
            services.push_back(svc);
        }
        // 加载失败不中断，继续尝试下一个
    }

    closedir(dir);
    return services;
}

void PluginLoader::unloadAll()
{
    for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
        // 销毁服务实例
        if (it->service) {
            // 尝试调用 destroyService，如果符号不存在则用 delete
            auto destroyFn = reinterpret_cast<DestroyServiceFunc>(
                dlsym(it->handle, "destroyService"));
            if (destroyFn) {
                destroyFn(it->service);
            } else {
                delete it->service;
            }
            it->service = nullptr;
        }

        // 关闭动态库
        if (it->handle) {
            dlclose(it->handle);
            it->handle = nullptr;
        }
    }
    plugins_.clear();
}

bool PluginLoader::unloadPlugin(const std::string& filepath)
{
    auto it = std::find_if(plugins_.begin(), plugins_.end(),
        [&filepath](const PluginEntry& e) { return e.filepath == filepath; });

    if (it == plugins_.end()) {
        lastError_ = "plugin not found: " + filepath;
        return false;
    }

    // 销毁服务
    if (it->service) {
        auto destroyFn = reinterpret_cast<DestroyServiceFunc>(
            dlsym(it->handle, "destroyService"));
        if (destroyFn) {
            destroyFn(it->service);
        } else {
            delete it->service;
        }
    }

    // 关闭动态库
    if (it->handle) {
        dlclose(it->handle);
    }

    plugins_.erase(it);
    return true;
}
