// ============================================================================
// ActionBridge.cpp — ROS Action ↔ CoreX RPC 映射 实现
// ============================================================================

#include "ActionBridge.hpp"
#include "DynamicServiceAdapter.hpp"
#include "RosNodeManager.hpp"
#include <ros/ros.h>
#include <sstream>
#include <thread>
#include <chrono>

ActionBridge::ActionBridge(const ActionMappingConfig& cfg)
    : config_(cfg)
{
    adapter_ = std::make_unique<DynamicServiceAdapter>(cfg.rpcService);

    // 注册四个 RPC 方法
    adapter_->registerMethod(cfg.rpcMethodStart,
        [this](const std::string& p) { return handleStart(p); });

    adapter_->registerMethod(cfg.rpcMethodCancel,
        [this](const std::string& p) { return handleCancel(p); });

    adapter_->registerMethod(cfg.rpcMethodFeedback,
        [this](const std::string& p) { return handleGetFeedback(p); });

    adapter_->registerMethod(cfg.rpcMethodResult,
        [this](const std::string& p) { return handleGetResult(p); });
}

ActionBridge::~ActionBridge()
{
    stop();
}

bool ActionBridge::start()
{
    if (started_.load()) return true;

    auto& nodeMgr = RosNodeManager::instance();
    if (!nodeMgr.isInitialized()) {
        ROS_ERROR("[ActionBridge] ROS not initialized");
        return false;
    }

    started_.store(true);

    ROS_INFO("[ActionBridge] Started: %s [%s] → %s",
             config_.rosAction.c_str(), config_.rosActionType.c_str(),
             config_.rpcService.c_str());

    return true;
}

void ActionBridge::stop()
{
    if (!started_.load()) return;

    {
        std::lock_guard<std::mutex> lock(goalMutex_);
        goals_.clear();
    }

    started_.store(false);
    ROS_INFO("[ActionBridge] Stopped: %s", config_.rosAction.c_str());
}

std::string ActionBridge::generateGoalId()
{
    uint64_t id = goalIdCounter_.fetch_add(1);
    std::ostringstream oss;
    oss << config_.rosAction << "_" << id;
    return oss.str();
}

int ActionBridge::activeGoalCount() const
{
    std::lock_guard<std::mutex> lock(goalMutex_);
    int count = 0;
    for (auto& kv : goals_) {
        if (!kv.second->finished) count++;
    }
    return count;
}

// ---- RPC Handlers ----

std::string ActionBridge::handleStart(const std::string& payload)
{
    if (!started_.load()) return "";

    std::string goalId = generateGoalId();
    auto goal = std::make_shared<TrackedGoal>();
    goal->goalId = goalId;

    {
        std::lock_guard<std::mutex> lock(goalMutex_);
        goals_[goalId] = goal;
    }

    ROS_INFO("[ActionBridge] Goal started: %s (id=%s)",
             config_.rosAction.c_str(), goalId.c_str());

    // TODO: 实际通过 Action Client 发送 Goal
    // 当前简化：直接返回 goal_id
    return goalId;
}

std::string ActionBridge::handleCancel(const std::string& payload)
{
    if (!started_.load()) return "";
    std::string goalId = payload;

    std::lock_guard<std::mutex> lock(goalMutex_);
    auto it = goals_.find(goalId);
    if (it == goals_.end()) return "";

    it->second->finished = true;
    goals_.erase(it);

    ROS_INFO("[ActionBridge] Goal cancelled: %s", goalId.c_str());
    return "OK";
}

std::string ActionBridge::handleGetFeedback(const std::string& payload)
{
    if (!started_.load()) return "";
    std::string goalId = payload;

    std::lock_guard<std::mutex> lock(goalMutex_);
    auto it = goals_.find(goalId);
    if (it == goals_.end()) return "";
    return it->second->feedbackData;
}

std::string ActionBridge::handleGetResult(const std::string& payload)
{
    if (!started_.load()) return "";
    std::string goalId = payload;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(config_.timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(goalMutex_);
            auto it = goals_.find(goalId);
            if (it == goals_.end()) return "";
            if (it->second->finished) {
                std::string result = it->second->resultData;
                goals_.erase(it);
                return result;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    {
        std::lock_guard<std::mutex> lock(goalMutex_);
        goals_.erase(goalId);
    }
    return "";  // timeout
}
