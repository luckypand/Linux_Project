#include "ConfigManager.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

bool ConfigManager::load(const std::string& filepath)
{
    try {
        root_ = YAML::LoadFile(filepath);
        if (!root_.IsDefined() || root_.IsNull()) {
            lastError_ = "empty or invalid YAML document";
            return false;
        }
        return true;
    } catch (const YAML::BadFile& e) {
        lastError_ = std::string("cannot open file: ") + e.what();
        return false;
    } catch (const YAML::ParserException& e) {
        lastError_ = std::string("YAML parse error: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = std::string("config error: ") + e.what();
        return false;
    }
}

YAML::Node ConfigManager::resolvePath(const std::string& path) const
{
    if (path.empty()) return root_;

    YAML::Node current = root_;
    std::istringstream ss(path);
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        if (!segment.empty()) {
            current.reset(current[segment]);
            if (!current.IsDefined()) {
                return YAML::Node();  // 返回未定义节点
            }
        }
    }

    return current;
}

std::string ConfigManager::getString(const std::string& path, const std::string& defaultValue) const
{
    YAML::Node node = resolvePath(path);
    if (!node.IsDefined() || node.IsNull()) return defaultValue;
    try {
        return node.as<std::string>();
    } catch (...) {
        // 尝试转为字符串（数字等）
        if (node.IsScalar()) {
            std::ostringstream oss;
            oss << node;
            return oss.str();
        }
        return defaultValue;
    }
}

int ConfigManager::getInt(const std::string& path, int defaultValue) const
{
    YAML::Node node = resolvePath(path);
    if (!node.IsDefined() || node.IsNull()) return defaultValue;
    try {
        return node.as<int>();
    } catch (...) {
        return defaultValue;
    }
}

bool ConfigManager::getBool(const std::string& path, bool defaultValue) const
{
    YAML::Node node = resolvePath(path);
    if (!node.IsDefined() || node.IsNull()) return defaultValue;
    try {
        return node.as<bool>();
    } catch (...) {
        return defaultValue;
    }
}

double ConfigManager::getDouble(const std::string& path, double defaultValue) const
{
    YAML::Node node = resolvePath(path);
    if (!node.IsDefined() || node.IsNull()) return defaultValue;
    try {
        return node.as<double>();
    } catch (...) {
        return defaultValue;
    }
}

std::vector<std::string> ConfigManager::getStringList(const std::string& path) const
{
    std::vector<std::string> result;
    YAML::Node node = resolvePath(path);
    if (!node.IsDefined() || !node.IsSequence()) return result;
    try {
        for (const auto& item : node) {
            result.push_back(item.as<std::string>());
        }
    } catch (...) {}
    return result;
}

YAML::Node ConfigManager::getNode(const std::string& path) const
{
    return resolvePath(path);
}

bool ConfigManager::hasPath(const std::string& path) const
{
    YAML::Node node = resolvePath(path);
    return node.IsDefined() && !node.IsNull();
}
