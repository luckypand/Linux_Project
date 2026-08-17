#include "HistoryCache.hpp"
#include <algorithm>

void HistoryCache::init(QoSType qos, uint32_t depth)
{
    qos_   = qos;
    depth_ = (qos == QoSType::LATCH) ? 1 : (depth > 0 ? depth : 1);
    count_ = 0;
    write_pos_ = 0;

    if (qos_ == QoSType::STREAM) {
        buffer_.clear();
    } else {
        buffer_.resize(depth_, UINT32_MAX);
    }
}

uint32_t HistoryCache::push(uint32_t block_id)
{
    uint32_t evicted = UINT32_MAX;

    switch (qos_) {
    case QoSType::STREAM:
        // 不缓存
        break;

    case QoSType::KEEP_LAST:
        evicted = buffer_[write_pos_];               // 即将被覆盖的旧值
        buffer_[write_pos_] = block_id;
        write_pos_ = (write_pos_ + 1) % depth_;
        if (count_ < depth_) count_++;
        break;

    case QoSType::LATCH:
        evicted = buffer_[0];                        // 旧值被覆盖
        buffer_[0] = block_id;
        count_ = 1;
        break;
    }

    return evicted;
}

std::vector<uint32_t> HistoryCache::getHistory() const
{
    std::vector<uint32_t> result;

    if (qos_ == QoSType::STREAM || count_ == 0) {
        return result;
    }

    if (qos_ == QoSType::LATCH) {
        if (buffer_[0] != UINT32_MAX) {
            result.push_back(buffer_[0]);
        }
        return result;
    }

    // KEEP_LAST: 从最旧到最新遍历环形缓冲区
    // write_pos_ 指向下一个写入位置，所以最旧的从 write_pos_ 开始
    result.reserve(count_);
    uint32_t start = (count_ < depth_) ? 0 : write_pos_;
    for (uint32_t i = 0; i < count_; i++) {
        uint32_t idx = (start + i) % depth_;
        if (buffer_[idx] != UINT32_MAX) {
            result.push_back(buffer_[idx]);
        }
    }

    return result;
}

uint32_t HistoryCache::size() const
{
    return count_;
}
