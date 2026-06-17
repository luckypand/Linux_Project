#include "ShmSegment.hpp"
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <iostream>
#include <unistd.h>
#include <sys/types.h>

ShmMemoryPool::ShmMemoryPool(const std::string& name,size_t size,Mode mode)
    :name_(name)
    ,size_(size)
    ,mapped_ptr_(nullptr)
    ,mode_(mode)
{
    if (mode_ == CREATE) {
        // 防止异常退出后残留同名共享内存
        shm_unlink(name_.c_str());

        // 打开共享内存对象 (可读写)，此时该对象还没有实际分配空间
        int fd = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) {
            throw std::runtime_error("shm_open (CREATE) failed");
        }
        // 设定对象大小
        if (ftruncate(fd, static_cast<long>(size_)) < 0) {
            close(fd);
            throw std::runtime_error("ftruncate failed");
        }
        // 映射当前进程的虚拟地址空间到共享内存
        mapped_ptr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);  // 映射完毕，文件描述符可关闭
    } else {
        // ATTACH 模式：直接打开已有共享内存段，不 unlink，不 ftruncate
        int fd = shm_open(name_.c_str(), O_RDWR, 0666);
        if (fd < 0) {
            throw std::runtime_error("shm_open (ATTACH) failed — is the server running?");
        }
        mapped_ptr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
    }

    if (MAP_FAILED == mapped_ptr_) {
        throw std::runtime_error("mmap failed");
    }
}

ShmMemoryPool::~ShmMemoryPool()
{
    if(MAP_FAILED != mapped_ptr_)
    {
        munmap(mapped_ptr_,size_);
    }
    shm_unlink(name_.c_str());
}

void* ShmMemoryPool::GetMappedptr()
{
    return mapped_ptr_;
}