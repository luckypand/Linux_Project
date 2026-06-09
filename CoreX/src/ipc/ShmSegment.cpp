#include "ShmSegment.hpp"
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <iostream>
#include <unistd.h>
#include <sys/types.h>

ShmMemoryPool::ShmMemoryPool(const std::string& name,size_t size)
    :name_(name)
    ,size_(size)
    ,mapped_ptr_(nullptr)
{
    //防止异常退出共享区，先尝试清理同名内存
    shm_unlink(name_.c_str());   

    //打开共享内存对象(可读可写)，此时仅创建了该对象没有实质分配空间
    int fd = shm_open(name_.c_str(),O_CREAT | O_RDWR,0666);
    if(fd < 0)
    {
        throw std::runtime_error("shm_open failed");
    }
    //分配物理大小
    if(ftruncate(fd,size_) < 0)
    {
        throw std::runtime_error("ftruncate failed");
    }
    //映射当前进程的虚拟地址空间到上述分配共享内存
    mapped_ptr_ = mmap(nullptr,size_,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
    if(MAP_FAILED == mapped_ptr_)
    {
        throw std::runtime_error("mmap failed");
    }
    close(fd);// 映射完毕，文件描述符可关闭
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