#pragma once
// ============================================================================
// ActionBridge.hpp — ROS Action ↔ CoreX RPC 映射适配器
//
// ROS Action (Goal/Feedback/Result/Cancel) 拆分为 RPC Methods:
//   StartNavigation(goal)    → 发起 Goal
//   CancelNavigation(id)     → 取消 Goal
//   GetNavigationFeedback(id)→ 轮询 Feedback
//   GetNavigationResult(id)  → 获取 Result
//
// 内部维护 goal_id → GoalHandle 映射表
// ============================================================================

#include "BridgeConfig.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>

class DynamicServiceAdapter;

class ActionBridge
{
public:
    explicit ActionBridge(const ActionMappingConfig& cfg);
    ~ActionBridge();

    ActionBridge(const ActionBridge&) = delete;
    ActionBridge& operator=(const ActionBridge&) = delete;

    bool start();
    void stop();

    DynamicServiceAdapter* getServiceAdapter() { return adapter_.get(); }
    int activeGoalCount() const;

private:
    struct TrackedGoal {
        std::string goalId;
        std::string resultData;
        std::string feedbackData;
        bool finished  = false;
        bool succeeded = false;
    };

    std::string handleStart(const std::string& payload);
    std::string handleCancel(const std::string& payload);
    std::string handleGetFeedback(const std::string& payload);
    std::string handleGetResult(const std::string& payload);
    std::string generateGoalId();

    ActionMappingConfig config_;
    std::unique_ptr<DynamicServiceAdapter> adapter_;

    mutable std::mutex goalMutex_;
    std::unordered_map<std::string, std::shared_ptr<TrackedGoal>> goals_;
    std::atomic<uint64_t> goalIdCounter_{0};
    std::atomic<bool> started_{false};
};
