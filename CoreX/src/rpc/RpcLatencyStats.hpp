/**
 * @file RpcLatencyStats.hpp
 * @brief 通用延迟统计工具 —— 直方图 / 分位数 / 时序桶 / 阶段分解
 *
 * 使用方式：
 *   1. 每次请求完成后调用 record(totalUs, breakdown, elapsedSec)
 *   2. 测试结束后调用 printAll() 输出完整分析报告
 *
 * 线程安全：内部使用 std::mutex 保护（适用于多线程收集场景）
 */

#pragma once

#include <vector>
#include <deque>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <mutex>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <limits>
#include <unordered_map>

// ============================================================
// 单次请求的阶段耗时分解
// ============================================================
struct LatencyBreakdown {
    std::string stage;   // 阶段名：deserialize / route / business / send
    double      us;      // 该阶段耗时（微秒）
};

// ============================================================
// 时序采样点（用于观察延迟随时间的变化趋势）
// ============================================================
struct TimeSeriesSample {
    double elapsedSec;   // 从测试开始到此刻的秒数
    double rttUs;        // 该请求的 RTT（或服务端处理时间）
};

// ============================================================
// 延迟分布统计器
// ============================================================
class RpcLatencyStats {
public:
    // ---------- 构造参数 ----------
    // name:            统计器名称（显示用）
    // maxSamples:      最大保存样本数（默认 100 万），超出后蓄水池采样
    // histMinUs:       直方图最低桶边界（微秒）
    // histMaxUs:       直方图最高桶边界（微秒）
    // histBuckets:     直方图桶数（对数分布）
    explicit RpcLatencyStats(const std::string& name = "default",
                             size_t maxSamples = 1'000'000,
                             double histMinUs = 1.0,
                             double histMaxUs = 10'000.0,
                             int histBuckets = 50)
        : name_(name)
        , maxSamples_(maxSamples)
        , histMinUs_(histMinUs)
        , histMaxUs_(histMaxUs)
        , histBuckets_(histBuckets)
    {
        histEdges_.resize(static_cast<size_t>(histBuckets_ + 1));
        histCounts_.resize(static_cast<size_t>(histBuckets_), 0);

        // 对数分布桶边界
        double logMin = std::log10(histMinUs_);
        double logMax = std::log10(histMaxUs_);
        for (int i = 0; i <= histBuckets_; ++i) {
            double logVal = logMin + (logMax - logMin) * i / histBuckets_;
            histEdges_[static_cast<size_t>(i)] = std::pow(10.0, logVal);
        }
    }

    // ---------- 记录一次请求的延迟 ----------
    // totalUs:    总延迟（微秒）
    // breakdown:  各阶段耗时分解（可选）
    // elapsedSec: 从测试开始到此刻的秒数（用于时序图，0 表示不记录）
    void record(double totalUs,
                const std::vector<LatencyBreakdown>& breakdown = {},
                double elapsedSec = 0.0)
    {
        std::lock_guard<std::mutex> lock(mtx_);

        totalCount_++;
        totalSum_ += totalUs;

        // 全量样本存储（带蓄水池降采样）
        if (samples_.size() < maxSamples_) {
            samples_.push_back(totalUs);
        } else {
            // 蓄水池采样: 以 maxSamples_ / totalCount_ 概率替换随机位置
            size_t idx = static_cast<size_t>(rand()) % totalCount_;
            if (idx < maxSamples_) {
                samples_[idx] = totalUs;
            }
        }

        // 更新 min/max
        if (totalUs < minUs_) minUs_ = totalUs;
        if (totalUs > maxUs_) maxUs_ = totalUs;

        // 直方图
        int bucket = findBucket(totalUs);
        if (bucket >= 0) {
            histCounts_[static_cast<size_t>(bucket)]++;
        }
        if (totalUs > histMaxUs_) {
            histOverflow_++;
        }
        if (totalUs < histMinUs_) {
            histUnderflow_++;
        }

        // 时序采样（每记录 100 次取一个点，避免撑爆内存）
        if (elapsedSec > 0.0 && totalCount_ % 100 == 0) {
            timeSeries_.push_back({elapsedSec, totalUs});
        }

        // 阶段分解累加
        for (const auto& bd : breakdown) {
            breakdownSum_[bd.stage] += bd.us;
        }
    }

    // ---------- 访问器 ----------
    size_t totalCount() const { return totalCount_; }
    const std::string& name() const { return name_; }

    // ---------- 延迟分位数（对样本排序后计算）----------
    double percentile(double p) const {
        std::lock_guard<std::mutex> lock(mtx_);
        return percentileUnsafe(p);
    }

    double min()   const { std::lock_guard<std::mutex> lock(mtx_); return totalCount_ > 0 ? minUs_ : 0.0; }
    double max()   const { std::lock_guard<std::mutex> lock(mtx_); return totalCount_ > 0 ? maxUs_ : 0.0; }
    double avg()   const { std::lock_guard<std::mutex> lock(mtx_); return totalCount_ > 0 ? totalSum_ / totalCount_ : 0.0; }
    double p50()   const { return percentile(50); }
    double p90()   const { return percentile(90); }
    double p95()   const { return percentile(95); }
    double p99()   const { return percentile(99); }
    double p999()  const { return percentile(99.9); }

    // ---------- 获取全量样本副本（用于外部分析）----------
    std::vector<double> getSamples() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return samples_;
    }

    // ---------- 获取阶段分解累加 ----------
    std::unordered_map<std::string, double> getBreakdownSum() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return breakdownSum_;
    }

    // ---------- 打印 ASCII 直方图 ----------
    void printHistogram(std::ostream& os = std::cout) const {
        std::lock_guard<std::mutex> lock(mtx_);

        os << "\n  ┌─ Latency Histogram [" << name_ << "] "
           << "(log-scale buckets, " << totalCount_ << " samples) ────────────────┐\n";

        // 找最大计数用于归一化条形宽度
        size_t maxCount = 0;
        for (size_t c : histCounts_) {
            if (c > maxCount) maxCount = c;
        }
        if (histOverflow_  > maxCount) maxCount = histOverflow_;
        if (histUnderflow_ > maxCount) maxCount = histUnderflow_;
        if (maxCount == 0) maxCount = 1;

        const int barWidth = 40;  // 最大条形宽度（字符）

        if (histUnderflow_ > 0) {
            printBar(os, "< underflow", histUnderflow_, maxCount, barWidth);
        }

        for (int i = 0; i < histBuckets_; ++i) {
            size_t idx = static_cast<size_t>(i);
            if (histCounts_[idx] == 0) continue;  // 跳过空桶
            std::ostringstream label;
            label << std::fixed << std::setprecision(1)
                  << histEdges_[idx] << "~" << histEdges_[idx + 1] << " us";
            printBar(os, label.str(), histCounts_[idx], maxCount, barWidth);
        }

        if (histOverflow_ > 0) {
            std::ostringstream label;
            label << "> " << std::fixed << std::setprecision(0)
                  << histMaxUs_ << " us (overflow)";
            printBar(os, label.str(), histOverflow_, maxCount, barWidth);
        }
        os << "  └──────────────────────────────────────────────────────────────────────────┘\n";
    }

    // ---------- 打印分位数摘要 ----------
    void printSummary(std::ostream& os = std::cout) const {
        double minV = 0, maxV = 0, avgV = 0, p50V = 0, p90V = 0, p95V = 0, p99V = 0, p999V = 0;
        size_t cnt = 0;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            cnt = totalCount_;
            if (cnt > 0) {
                minV  = minUs_;
                maxV  = maxUs_;
                avgV  = totalSum_ / totalCount_;
                p50V  = percentileUnsafe(50);
                p90V  = percentileUnsafe(90);
                p95V  = percentileUnsafe(95);
                p99V  = percentileUnsafe(99);
                p999V = percentileUnsafe(99.9);
            }
        }

        os << "\n  ┌─ Latency Summary [" << name_ << "] ────────────────────┐\n";
        os << "  │ Samples:     " << std::setw(10) << cnt               << "                │\n";
        os << "  │ Min:         " << std::setw(10) << std::fixed << std::setprecision(2)
           << minV   << " us             │\n";
        os << "  │ Avg:         " << std::setw(10) << std::fixed << std::setprecision(2)
           << avgV   << " us             │\n";
        os << "  │ P50 (median):" << std::setw(10) << std::fixed << std::setprecision(2)
           << p50V   << " us             │\n";
        os << "  │ P90:         " << std::setw(10) << std::fixed << std::setprecision(2)
           << p90V   << " us             │\n";
        os << "  │ P95:         " << std::setw(10) << std::fixed << std::setprecision(2)
           << p95V   << " us             │\n";
        os << "  │ P99:         " << std::setw(10) << std::fixed << std::setprecision(2)
           << p99V   << " us             │\n";
        os << "  │ P99.9:       " << std::setw(10) << std::fixed << std::setprecision(2)
           << p999V  << " us             │\n";
        os << "  │ Max:         " << std::setw(10) << std::fixed << std::setprecision(2)
           << maxV   << " us             │\n";

        // 尾延迟放大比
        if (p50V > 0.0) {
            double p99ratio  = p99V  / p50V;
            double p999ratio = p999V / p50V;
            os << "  │ P99 / P50:   " << std::setw(10) << std::fixed << std::setprecision(1)
               << p99ratio << "x";
            if (p99ratio > 10.0)       os << "  ⚠ 严重长尾";
            else if (p99ratio > 3.0)   os << "  ⚠ 明显长尾";
            else                       os << "  ✓ 正常";
            os << "      │\n";
            os << "  │ P999 / P50:  " << std::setw(10) << std::fixed << std::setprecision(1)
               << p999ratio << "x";
            if (p999ratio > 50.0)      os << "  ⚠ P999严重长尾";
            else if (p999ratio > 10.0) os << "  ⚠ P999明显长尾";
            else                       os << "  ✓ 正常";
            os << "  │\n";
        }
        os << "  └────────────────────────────────────────────────────┘\n";
    }

    // ---------- 打印阶段耗时分解 ----------
    void printBreakdown(std::ostream& os = std::cout) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (breakdownSum_.empty()) return;

        double total = 0.0;
        for (const auto& kv : breakdownSum_) total += kv.second;
        if (total <= 0.0 || totalCount_ == 0) return;

        os << "\n  ┌─ Stage Breakdown (avg per request, " << totalCount_
           << " requests) ────────────┐\n";
        for (const auto& kv : breakdownSum_) {
            double avgPerReq = kv.second / totalCount_;
            double pct = 100.0 * kv.second / total;
            os << "  │ " << std::setw(14) << kv.first << ": "
               << std::setw(8) << std::fixed << std::setprecision(1)
               << avgPerReq << " us  ("
               << std::setw(5) << std::setprecision(1) << pct << "%)       │\n";
        }
        os << "  └──────────────────────────────────────────────────────────────┘\n";
    }

    // ---------- 打印时序视图（延迟随时间变化）----------
    void printTimeSeries(std::ostream& os = std::cout) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (timeSeries_.empty()) {
            os << "  (no time-series data)\n";
            return;
        }

        double p50V = totalCount_ > 0 ? percentileUnsafe(50) : 0.0;
        double p99V = totalCount_ > 0 ? percentileUnsafe(99) : 0.0;

        os << "\n  ┌─ Latency Over Time [" << name_ << "] "
           << "(every 100th request, " << timeSeries_.size() << " points) ──────────────┐\n";

        // 计算时间跨度
        double tMin = timeSeries_.front().elapsedSec;
        double tMax = timeSeries_.back().elapsedSec;
        double tRange = tMax - tMin;
        if (tRange <= 0.0) tRange = 1.0;

        const int plotHeight = 15;
        const int plotWidth  = 50;
        double yMax = p99V * 1.5;  // Y 轴上限为 P99 的 1.5 倍
        if (yMax <= 0.0) yMax = 100.0;

        // 将时间序列聚合到高度桶
        std::vector<double> bucketMin(static_cast<size_t>(plotHeight),
                                       std::numeric_limits<double>::max());
        std::vector<double> bucketMax(static_cast<size_t>(plotHeight), 0.0);
        std::vector<double> bucketAvg(static_cast<size_t>(plotHeight), 0.0);
        std::vector<size_t> bucketCount(static_cast<size_t>(plotHeight), 0);

        for (const auto& ts : timeSeries_) {
            int bucket = static_cast<int>(
                (ts.elapsedSec - tMin) / tRange * plotHeight);
            if (bucket < 0) bucket = 0;
            if (bucket >= plotHeight) bucket = plotHeight - 1;
            size_t idx = static_cast<size_t>(bucket);
            if (ts.rttUs < bucketMin[idx]) bucketMin[idx] = ts.rttUs;
            if (ts.rttUs > bucketMax[idx]) bucketMax[idx] = ts.rttUs;
            bucketAvg[idx] += ts.rttUs;
            bucketCount[idx]++;
        }
        for (size_t i = 0; i < static_cast<size_t>(plotHeight); ++i) {
            if (bucketCount[i] > 0) bucketAvg[i] /= static_cast<double>(bucketCount[i]);
        }

        for (int row = plotHeight - 1; row >= 0; --row) {
            size_t idx = static_cast<size_t>(row);
            double tLabel = tMin + tRange * (row + 0.5) / plotHeight;
            os << "  │ " << std::fixed << std::setprecision(2) << std::setw(7)
               << tLabel << "s │";

            if (bucketCount[idx] > 0) {
                double avgVal = bucketAvg[idx];
                int barLen = static_cast<int>(avgVal / yMax * plotWidth);
                if (barLen > plotWidth) barLen = plotWidth;

                int minBar = static_cast<int>(bucketMin[idx] / yMax * plotWidth);
                int maxBar = static_cast<int>(bucketMax[idx] / yMax * plotWidth);
                if (maxBar >= plotWidth) maxBar = plotWidth - 1;

                for (int x = 0; x < plotWidth; ++x) {
                    if (x == barLen) os << "\033[1;32m█\033[0m";
                    else if (x >= minBar && x <= maxBar) os << "\033[2m·\033[0m";
                    else os << " ";
                }
                os << " " << std::fixed << std::setprecision(1)
                   << avgVal << "us";
            }
            os << "\n";
        }

        // X 轴
        os << "  │         └";
        for (int x = 0; x < plotWidth; ++x) os << "─";
        os << "\n  │         0us";
        for (int x = 0; x < plotWidth - 12; ++x) os << " ";
        os << std::fixed << std::setprecision(0) << yMax << "us"
           << " (max ~P99×1.5)\n";

        // 参考线标注
        os << "  │  P50=" << std::fixed << std::setprecision(1) << p50V << "us"
           << "  P99=" << p99V << "us"
           << "  Max=" << (totalCount_ > 0 ? maxUs_ : 0.0) << "us\n";
        os << "  └───────────────────────────────────────────────────────────────────────────┘\n";
    }

    // ---------- 一键打印全部 ----------
    void printAll(std::ostream& os = std::cout) const {
        printSummary(os);
        printHistogram(os);
        printBreakdown(os);
        printTimeSeries(os);
    }

    // ---------- 合并另一个统计器（用于聚合多线程结果）----------
    void merge(const RpcLatencyStats& other) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::lock_guard<std::mutex> otherLock(other.mtx_);

        totalCount_ += other.totalCount_;
        totalSum_   += other.totalSum_;

        if (other.minUs_ < minUs_) minUs_ = other.minUs_;
        if (other.maxUs_ > maxUs_) maxUs_ = other.maxUs_;

        // 样本直接合并
        samples_.insert(samples_.end(),
                        other.samples_.begin(), other.samples_.end());

        // 直方图累加
        for (size_t i = 0; i < histCounts_.size() && i < other.histCounts_.size(); ++i) {
            histCounts_[i] += other.histCounts_[i];
        }
        histOverflow_  += other.histOverflow_;
        histUnderflow_ += other.histUnderflow_;

        // 阶段分解累加
        for (const auto& kv : other.breakdownSum_) {
            breakdownSum_[kv.first] += kv.second;
        }

        // 时序合并（按 elapsedSec 排序后拼接）
        timeSeries_.insert(timeSeries_.end(),
                           other.timeSeries_.begin(), other.timeSeries_.end());
        std::sort(timeSeries_.begin(), timeSeries_.end(),
                  [](const auto& a, const auto& b) { return a.elapsedSec < b.elapsedSec; });
    }

private:
    // ---------- 内部：不加锁的分位数计算 ----------
    double percentileUnsafe(double p) const {
        if (samples_.empty()) return 0.0;
        std::vector<double> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * p / 100.0);
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }

    int findBucket(double value) const {
        if (value < histMinUs_ || value > histMaxUs_) return -1;
        auto it = std::upper_bound(histEdges_.begin(), histEdges_.end(), value);
        if (it == histEdges_.begin()) return -1;
        int idx = static_cast<int>(it - histEdges_.begin()) - 1;
        if (idx >= histBuckets_) idx = histBuckets_ - 1;
        return idx;
    }

    void printBar(std::ostream& os, const std::string& label,
                  size_t count, size_t maxCount, int barWidth) const
    {
        os << "  │ " << std::setw(30) << std::left << label << " │";
        int barLen = static_cast<int>(static_cast<double>(count) / maxCount * barWidth);
        double pct = totalCount_ > 0 ? (100.0 * count / totalCount_) : 0.0;

        for (int i = 0; i < barWidth; ++i) {
            if (i < barLen) {
                if (pct > 10.0)      os << "\033[1;31m█\033[0m";  // 红色
                else if (pct > 1.0)  os << "\033[1;33m█\033[0m";  // 黄色
                else                 os << "\033[1;32m█\033[0m";  // 绿色
            } else {
                os << " ";
            }
        }
        os << " " << std::setw(8) << count
           << " (" << std::fixed << std::setprecision(1) << std::setw(5) << pct << "%)\n";
    }

    // ---- 成员变量 ----
    std::string name_;
    size_t      maxSamples_;
    size_t      totalCount_ = 0;
    double      totalSum_   = 0.0;
    double      minUs_      = std::numeric_limits<double>::max();
    double      maxUs_      = 0.0;

    // 全量样本（用于分位数计算）
    std::vector<double> samples_;

    // 对数直方图
    double              histMinUs_;
    double              histMaxUs_;
    int                 histBuckets_;
    std::vector<double> histEdges_;
    std::vector<size_t> histCounts_;
    size_t              histOverflow_  = 0;
    size_t              histUnderflow_ = 0;

    // 阶段分解
    std::unordered_map<std::string, double> breakdownSum_;

    // 时序采样
    std::deque<TimeSeriesSample> timeSeries_;

    mutable std::mutex mtx_;
};
