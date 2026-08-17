#include "TopicManager.hpp"
#include "BufferPool.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <algorithm>

// ============================================================
// 构造/析构
// ============================================================

TopicManager::TopicManager(const std::string& pubsub_name, const std::string& pool_name,
                           uint32_t max_topics, uint32_t block_count, uint32_t max_payload,
                           uint32_t max_subs, uint32_t ring_cap)
    : pubsub_name_(pubsub_name), pool_name_(pool_name), is_creator_(true)
{
    initShmCreator(pubsub_name, pool_name, max_topics, block_count, max_payload,
                   max_subs, ring_cap);
}

TopicManager::TopicManager(const std::string& pubsub_name, const std::string& pool_name)
    : pubsub_name_(pubsub_name), pool_name_(pool_name), is_creator_(false)
{
    initShmAttacher(pubsub_name, pool_name);
}

TopicManager::~TopicManager()
{
    stopHeartbeatThread();

    channels_.clear();

    if (pubsub_mapped_ && pubsub_mapped_ != MAP_FAILED) {
        munmap(pubsub_mapped_, pubsub_size_);
    }
    if (is_creator_) {
        shm_unlink(pubsub_name_.c_str());
    }
}

// ============================================================
// SHM 初始化
// ============================================================

void TopicManager::initShmCreator(const std::string& ps_name, const std::string& pool_name,
                                  uint32_t max_topics, uint32_t block_count, uint32_t max_payload,
                                  uint32_t max_subs, uint32_t ring_cap)
{
    // 1. 创建 BufferPool
    pool_.reset(new BufferPool(pool_name, block_count, max_payload));

    // 2. 计算 PubSub 段大小
    uint32_t slot_size = TopicChannelHeader::requiredSize(max_subs, ring_cap) + MAX_TOPIC_NAME;
    pubsub_size_ = PubSubSegmentHeader::requiredSize(max_topics, slot_size);

    // 3. 创建 PubSub SHM 段
    shm_unlink(ps_name.c_str());
    pubsub_fd_ = shm_open(ps_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (pubsub_fd_ < 0)
        throw std::runtime_error("TopicManager: shm_open(CREATE) failed");

    if (ftruncate(pubsub_fd_, pubsub_size_) < 0) {
        close(pubsub_fd_);
        throw std::runtime_error("TopicManager: ftruncate failed");
    }

    pubsub_mapped_ = mmap(nullptr, pubsub_size_, PROT_READ | PROT_WRITE, MAP_SHARED, pubsub_fd_, 0);
    close(pubsub_fd_);
    pubsub_fd_ = -1;

    if (pubsub_mapped_ == MAP_FAILED)
        throw std::runtime_error("TopicManager: mmap failed");

    // 4. 初始化头部
    header_ = new (pubsub_mapped_) PubSubSegmentHeader();
    header_->magic          = PUBSUB_MAGIC;
    header_->version        = PUBSUB_VERSION;
    header_->max_topics     = max_topics;
    header_->topic_count    = 0;
    header_->max_subscribers = max_subs;
    header_->ring_capacity  = ring_cap;
    header_->topic_slot_size = slot_size;

    // 5. 清空所有 Topic 槽位（name[0]='\0' 表示空闲）
    for (uint32_t i = 0; i < max_topics; i++) {
        uint8_t* base = slotBase(i);
        std::memset(base, 0, slot_size);
    }

    // 预分配 channels_ 数组
    channels_.resize(max_topics);
}

void TopicManager::initShmAttacher(const std::string& ps_name, const std::string& pool_name)
{
    // 1. 附加 BufferPool
    pool_.reset(new BufferPool(pool_name));

    // 2. 附加 PubSub 段
    pubsub_fd_ = shm_open(ps_name.c_str(), O_RDWR, 0666);
    if (pubsub_fd_ < 0)
        throw std::runtime_error("TopicManager: shm_open(ATTACH) failed");

    // 先映射头部
    void* probe = mmap(nullptr, sizeof(PubSubSegmentHeader), PROT_READ | PROT_WRITE,
                       MAP_SHARED, pubsub_fd_, 0);
    if (probe == MAP_FAILED) { close(pubsub_fd_); throw std::runtime_error("mmap probe failed"); }

    auto* hdr = static_cast<PubSubSegmentHeader*>(probe);
    if (hdr->magic != PUBSUB_MAGIC) {
        munmap(probe, sizeof(PubSubSegmentHeader));
        close(pubsub_fd_);
        throw std::runtime_error("TopicManager: invalid magic");
    }

    pubsub_size_ = PubSubSegmentHeader::requiredSize(hdr->max_topics, hdr->topic_slot_size);
    munmap(probe, sizeof(PubSubSegmentHeader));

    // 完整映射
    pubsub_mapped_ = mmap(nullptr, pubsub_size_, PROT_READ | PROT_WRITE, MAP_SHARED, pubsub_fd_, 0);
    close(pubsub_fd_);
    pubsub_fd_ = -1;

    if (pubsub_mapped_ == MAP_FAILED)
        throw std::runtime_error("TopicManager: mmap full failed");

    header_ = static_cast<PubSubSegmentHeader*>(pubsub_mapped_);
    channels_.resize(header_->max_topics);

    // 为已有 Topic 创建本地 Channel 对象
    for (uint32_t i = 0; i < header_->max_topics; i++) {
        uint8_t* base = slotBase(i);
        if (base[0] != '\0') {  // 槽位被占用（name 非空）
            auto ch = std::make_unique<TopicChannel>();
            ch->attach(base + MAX_TOPIC_NAME, pool_.get());
            channels_[i] = std::move(ch);
        }
    }
}

// ============================================================
// Topic 管理
// ============================================================

int32_t TopicManager::createTopic(const std::string& name, const QosPolicy& qos)
{
    if (name.empty() || name.size() >= MAX_TOPIC_NAME) return -1;

    // 检查是否已存在
    for (uint32_t i = 0; i < header_->max_topics; i++) {
        uint8_t* base = slotBase(i);
        if (base[0] != '\0' && std::strcmp(reinterpret_cast<char*>(base), name.c_str()) == 0) {
            return static_cast<int32_t>(i);  // 已存在，返回已有 id
        }
    }

    // 扫描空闲槽位
    for (uint32_t i = 0; i < header_->max_topics; i++) {
        uint8_t* base = slotBase(i);
        if (base[0] != '\0') continue;  // 已占用

        // 原子抢占：用 '\1' 标记占用中（简化，非 CAS）
        // v1 简化：单进程创建 Topic，不加锁
        std::strncpy(reinterpret_cast<char*>(base), name.c_str(), MAX_TOPIC_NAME - 1);

        auto ch = std::make_unique<TopicChannel>();
        ch->init(base + MAX_TOPIC_NAME, i, qos, pool_.get(),
                 header_->max_subscribers, header_->ring_capacity);
        channels_[i] = std::move(ch);

        header_->topic_count++;
        return static_cast<int32_t>(i);
    }

    return -1;  // 槽位满
}

TopicChannel* TopicManager::getChannel(uint32_t topic_id)
{
    if (topic_id >= header_->max_topics) return nullptr;
    return channels_[topic_id].get();
}

int32_t TopicManager::findTopic(const std::string& name) const
{
    for (uint32_t i = 0; i < header_->max_topics; i++) {
        const uint8_t* base = slotBase(i);
        if (base[0] != '\0' && std::strcmp(reinterpret_cast<const char*>(base), name.c_str()) == 0) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// ============================================================
// 快捷操作
// ============================================================

uint32_t TopicManager::publish(uint32_t topic_id, const void* data, uint32_t size)
{
    TopicChannel* ch = getChannel(topic_id);
    if (!ch) return UINT32_MAX;
    return ch->publish(data, size);
}

int32_t TopicManager::subscribe(uint32_t topic_id)
{
    TopicChannel* ch = getChannel(topic_id);
    if (!ch) return -1;
    return ch->subscribe();
}

void TopicManager::unsubscribe(uint32_t topic_id, uint32_t sub_id)
{
    TopicChannel* ch = getChannel(topic_id);
    if (ch) ch->unsubscribe(sub_id);
}

bool TopicManager::receive(uint32_t topic_id, uint32_t sub_id, void* buf, uint32_t& size)
{
    TopicChannel* ch = getChannel(topic_id);
    if (!ch) return false;
    return ch->receive(sub_id, buf, size);
}

void TopicManager::heartbeat(uint32_t topic_id, uint32_t sub_id, uint64_t now_us)
{
    TopicChannel* ch = getChannel(topic_id);
    if (ch) ch->heartbeat(sub_id, now_us);
}

// ============================================================
// 心跳恢复
// ============================================================

void TopicManager::startHeartbeatThread(uint64_t interval_us, uint64_t timeout_us)
{
    if (hb_running_.load()) return;
    hb_running_.store(true);

    hb_thread_ = std::thread([this, interval_us, timeout_us]() {
        while (hb_running_.load(std::memory_order_acquire)) {
            scanRecover(timeout_us);
            std::this_thread::sleep_for(std::chrono::microseconds(interval_us));
        }
    });
}

void TopicManager::stopHeartbeatThread()
{
    hb_running_.store(false);
    if (hb_thread_.joinable()) {
        hb_thread_.join();
    }
}

void TopicManager::recoverOnce(uint64_t timeout_us)
{
    scanRecover(timeout_us);
}

void TopicManager::scanRecover(uint64_t timeout_us)
{
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    for (uint32_t i = 0; i < header_->max_topics; i++) {
        TopicChannel* ch = channels_[i].get();
        if (!ch) continue;

        auto timed_out = ch->checkTimeout(now, timeout_us);
        for (uint32_t sub_id : timed_out) {
            ch->unsubscribe(sub_id);
        }
    }
}

// ============================================================
// 内部辅助
// ============================================================

uint8_t* TopicManager::slotBase(uint32_t idx)
{
    return static_cast<uint8_t*>(pubsub_mapped_)
           + sizeof(PubSubSegmentHeader)
           + idx * header_->topic_slot_size;
}

const uint8_t* TopicManager::slotBase(uint32_t idx) const
{
    return static_cast<const uint8_t*>(pubsub_mapped_)
           + sizeof(PubSubSegmentHeader)
           + idx * header_->topic_slot_size;
}
