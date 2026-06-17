#include "RingBuffer.hpp"

bool ShmRingBuffer::CAS_Push(const void* data_addr,size_t len)
{
    //写入长度大于单个block块
    if(len > sizeof(ShmBlock::data))
    {
        return false;
    }
    size_t current_write;
    size_t next_write;

    //CAS更新读写区 
    do{
        current_write = write_index.load(std::memory_order_relaxed);
        next_write = (current_write + 1) % RING_CAPACITY;

        if(next_write == read_index.load(std::memory_order_acquire))//判断写满了
        {
            return false; //写满了
        }
    }while(!write_index.compare_exchange_weak(
        current_write,next_write,
        std::memory_order_release,std::memory_order_relaxed
    ));
    //CAS的方式更新状态机
    ShmBlock& block = blocks[current_write]; //记录当前写的区间块
    BlockState expected = BlockState::FREE;
    if(!block.state.compare_exchange_strong(expected,BlockState::WRITING))//状态机强写入写状态
    {
        return false;
    }
    //实际拷贝数据,标记block块长度
    block.data_len = len;
    std::memcpy(block.data,data_addr,len);
    //写入完成，标记可读
    block.state.store(BlockState::READY,std::memory_order_release);
    return true;
}
//MPSC
bool ShmRingBuffer::CAS_Pop(std::string& out_data)
{
    size_t current_read = read_index.load(std::memory_order_relaxed);
    if(current_read == write_index)
    {
        return false;
    }

    ShmBlock& block = blocks[current_read];

    if(block.state.load(std::memory_order_acquire) != BlockState::READY)
    {
        return false;
    }
    out_data.assign(block.data,block.data_len);

    BlockState expected = BlockState::READY;
    block.state.compare_exchange_strong(expected,BlockState::FREE);
    read_index.store((read_index + 1) % RING_CAPACITY,std::memory_order_release);
    return true;
}