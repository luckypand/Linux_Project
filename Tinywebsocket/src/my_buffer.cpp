#include "my_buffer.hpp"
#include <algorithm>
#include <cerrno>

//|<-----prependable----><-----readable----><-----writeable---->|
//|                     r                  w                    size

Buffer::Buffer(int Buffersize)
    :buffer_(Buffersize)
    ,readPos_(0)
    ,writePos_(0)
{
    
}

//可写字节空间
size_t Buffer::WritableBytes() const
{
    return buffer_.size() - writePos_;
} 

//可读字节空间
size_t Buffer::ReadableBytes() const 
{
    return writePos_ - readPos_;
}

//预留字节空间
size_t Buffer::PrependableBytes() const
{
    return readPos_;
}

//空间不够，vector开辟空间
void Buffer::MakeSpace_(size_t len)
{
    //情况1.可写空间 + 预存空间不足，重新分配
    //情况2.写空间不足 ，预存空间足够，则搬运原数据到预存数据开头
    if(len > WritableBytes() + PrependableBytes())
    {
        buffer_.resize(writePos_ + len + 1); //resize放入实际可能需要写入的数量
    }
    else
    {
        std::copy(BeginPtr() + readPos_,BeginPtr() + writePos_,BeginPtr()); 
        readPos_ = 0;
        writePos_ = ReadableBytes();
    }
}

//判断是否足够写入并处理
void Buffer::EnsureWriteable(size_t len)
{
    if(len > WritableBytes()) {
        MakeSpace_(len);
    }
    assert(len <= WritableBytes());    
}

char* Buffer::BeginWrite()
{
    return &buffer_[writePos_];
}

char* Buffer::BeginRead()
{
    return &buffer_[readPos_];    
}

char* Buffer::BeginPtr()
{
    return &buffer_[0];
}

//移动写入后的下标
void Buffer::HasWritten(size_t len)
{
    writePos_ += len;
}

//追加写入指定长度字符,默认从写区开始写
void Buffer::Append(const char* str, size_t len)
{
    assert(str);
    EnsureWriteable(len);   // 确保可写的长度
    std::copy(str, str + len, BeginWrite());    // 将str放到写下标开始的地方
    HasWritten(len);    // 移动写下标
}

//追加写入字符串
void Buffer::Append(const std::string& str)
{
    assert(str.c_str());
    Append(str.c_str(),str.size());
}

//用法说明?
void Buffer::Append(const void* data, size_t len)
{
    assert(data);
    Append(static_cast<const char *>(data),len);
}

//追加写入Buffer：Append默认从写区开始写，调用只需要提供需要被写入的地址和长度
void Buffer::Append(const Buffer& buff)
{
    size_t readbytes = buff.ReadableBytes();
    Append(&buff.buffer_[buff.readPos_],readbytes);
}

//将buffer中可读的区域写入fd中
ssize_t Buffer::WriteFd(int fd, int* Errno)
{
    ssize_t len = write(fd,BeginRead(),ReadableBytes());
    if(len < 0)
    {
        *Errno = errno;
        return len;
    }
    readPos_ += len;
    return len;
}

//从fd上读取数据，放到可写区域
ssize_t Buffer::ReadFd(int fd, int* Errno)
{
    char buff[65536];
    struct iovec iov[2];//分散读，iovec结构体
    size_t writeable = WritableBytes(); //先记录能写多少
    iov[0].iov_base = &buffer_[writePos_]; //第一个
    iov[0].iov_len = writeable;
    iov[1].iov_base = buff; //第二个
    iov[1].iov_len = sizeof(buff);
    ssize_t len = readv(fd, iov, 2);
    if(len < 0) {
        *Errno = errno;
    } else if(static_cast<size_t>(len) <= writeable) {   //若len小于writable，说明写区可以容纳len
        writePos_ += len;   //直接移动写下标
    } else {    //写区写满了,下标移到最后,剩余的长度放到buff中
        writePos_ = buffer_.size();
        Append(buff, static_cast<size_t>(len - writeable)); //剩余的长度
    }
    return len;
}

//将buffer中的可读区域转换为string后退出，清空数据内容，并重置读写下标
std::string Buffer::RetrieveAllToStr()
{
    std::string str(BeginRead(), ReadableBytes());
    //清空数据内容
    memset(&buffer_[0], 0, buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
    return str;
}