#include <cstring>
#include <iostream>
#include <atomic>

constexpr size_t BLOCK_SIZE = 4096;  // 每个Block的总大小 (可容纳 ~4080 字节 payload)
constexpr size_t RING_CAPACITY = 16; // 环形队列容量

enum class BlockState:uint8_t
{
    FREE = 0,
    WRITING = 1,
    READY = 2 
};

struct alignas(64) ShmBlock
{ 
    std::atomic<BlockState> state;
    size_t data_len;
    char data[BLOCK_SIZE - sizeof(std::atomic<BlockState>) - sizeof(data_len)];
};

struct ShmRingBuffer
{
    alignas(64) std::atomic<size_t> write_index{0};
    alignas(64) std::atomic<size_t> read_index{0};

    ShmBlock blocks[RING_CAPACITY];

    ShmRingBuffer()
    {
        for(size_t i = 0;i < RING_CAPACITY;i++)
        {
            blocks[i].state.store(BlockState::FREE);
        }
    }
    ~ShmRingBuffer() = default;

    bool CAS_Push(const void* data_addr,size_t len);
    bool CAS_Pop(std::string& out_data);
};