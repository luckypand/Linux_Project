#include "Buffer.hpp"
#include <assert.h>
#include <iterator> 
#include <sys/uio.h>
#include <unistd.h>

Buffer::Buffer(size_t buffer_size)
    :buffer_(kInitBytes + kCheapPretendBytes) 
    ,ReadIndex_(kCheapPretendBytes)         
    ,WriteIndex_(kCheapPretendBytes)
{
    //此处直接传入为而不使用reserve是为了获得合法空间位置
    //因为本质对该vector的使用不是通过push_back挨个放入
    //通过初始化buffer_使得buffer能够在一开始获得合法空间，并使得
    //size在后续扩容时直接等于capacity，避免浪费
}

/*
* @brief:
*     返回可读区起始指针
*/
const char* Buffer::peek() const
{
    return this->begin() + ReadIndex_;
}

/*
* @brief:
*     消费指定长度的可读数据
* @parameter:
      len:本次消费的数据长度
*/
void Buffer::retrieve(size_t len)
{
    assert(len <= ReadBytes());
    ReadIndex_ += len;
}

/*
* @brief:
*     消费全部可读数据
*/
void Buffer::retrieveAll()
{
    ReadIndex_ = kCheapPretendBytes;
    WriteIndex_ = kCheapPretendBytes;
}

/*
* @brief:
*     以 string 形式取出全部可读数据
*/
std::string Buffer::retrieveAsString()
{
    std::string str(this->peek(),this->ReadBytes());
    retrieveAll();
    return str;
}

/*
* @brief:
*     取出指定长度的可读数据并返回 string
* @parameter:
      len:本次取出的数据长度
*/
std::string Buffer::retrieveAsString(size_t len)
{
    assert(len <= this->ReadBytes());
    std::string str(this->peek(),len);
    retrieve(len);
    return str;
}

/*
* @brief:
*     追加原始字节数据
* @parameter:
      data:待写入的数据
      len:待写入的数据长度
*/
void Buffer::append(const char* data, size_t len)
{
    this->ensureWriteable(len);
    std::copy(data, data + len, std::next(buffer_.begin(), static_cast<std::ptrdiff_t>(WriteIndex_)));
    WriteIndex_ += len;
}

/*
* @brief:
*     追加 string 数据
* @parameter:
      data:待写入的字符串
*/
void Buffer::append(const std::string& data)
{
    this->append(data.data(),data.size());
}

/*
* @brief:
*     返回可写区起始指针
*/
const char* Buffer::beginWrite() const
{
    return this->begin() + WriteIndex_;
}

/*
* @brief:
*     扩容或搬移数据
* @parameter:
      len:本次写入的数据长度
*/
void Buffer::makespace(size_t len)
{
    //可写空间加上预留空间大于需写长度，直接搬移
    //可写加预留小于需要长度，进行resize扩容
    if(WriteBytes() + PretendBytes() < len)
    {
        buffer_.resize(WriteIndex_ + len);
    }
    else
    {
        size_t ReadableByte = this->ReadBytes();

        //拷贝可读到kCheapPretendBytes，重置指针
        //检查是否和原本的可读范围一致

        std::copy(std::next(buffer_.begin(), static_cast<std::ptrdiff_t>(ReadIndex_)),
          std::next(buffer_.begin(), static_cast<std::ptrdiff_t>(WriteIndex_)),
          std::next(buffer_.begin(), static_cast<std::ptrdiff_t>(kCheapPretendBytes)));

        ReadIndex_ = kCheapPretendBytes;          
        WriteIndex_ = kCheapPretendBytes + ReadableByte;
        assert(ReadableByte == ReadBytes());
    }
}

/*
* @brief:
*     保证写区足够
*/
void Buffer::ensureWriteable(size_t len)
{
    if(WriteBytes() < len)
    {
        makespace(len);
    }
    assert(WriteBytes() >= len);
}

/*
* @brief:
*     在可读区前面写入（不移动可读区内容）
*/
void Buffer::prepend(const void* data, size_t len)
{
    assert(len <= PretendBytes());
    ReadIndex_ -= len;
    const char* d = static_cast<const char*>(data);
    std::copy(d, d + len, std::next(begin(), static_cast<std::ptrdiff_t>(ReadIndex_)));
}

/*
* @brief:
*     手动推进写指针（与 beginWrite 配对）
*/
void Buffer::hasWritten(size_t len)
{
    assert(len <= WriteBytes());
    WriteIndex_ += len;
}

/*
* @brief:
*     从 fd 读取数据到 Buffer
* @parameter:
      fd:文件描述符
      savedErrno:保存错误码的指针
*/
ssize_t Buffer::readFd(int fd, int* savedErrno)   
{
    //开辟两块分散空间，先进行大致判断，若空间足够则使用一块，否则
    //使用两块空间寄存数据。后续依据readv的返回值判断需不需要将第二块
    //的内容扩容写入
    char extrabuff[65536];
    struct iovec vec[2];
    vec[0].iov_base = this->beginWrite();
    vec[0].iov_len = this->WriteBytes();
    vec[1].iov_base = extrabuff;
    vec[1].iov_len = sizeof(extrabuff);
    
    const size_t writeable = this->WriteBytes(); //先记录下当前还有多少空间可以写

    const int iovcnt = (writeable < sizeof(extrabuff) ? 2 : 1);
    ssize_t n = ::readv(fd,vec,iovcnt);

    if(n < 0) //注意此处使用if因为epoll_wait是LT模式,就算有数据没读完下次醒来也能读
    {
        *savedErrno = errno;
        // std::cerr << *savedErrno << std::endl; //禁止使用打印，否则将会
    }
    else if(static_cast<size_t>(n) <= writeable)//第一块就读完了
    {
        WriteIndex_ += n;//更新可写区域
    }
    else//还有数据在第二块中，扩容
    {
        WriteIndex_ = buffer_.size();
        this->append(extrabuff,n - writeable);
    }
    return n;
}

/*
* @brief:
*     从Buffer写入数据到fd
* @parameter:
      fd:文件描述符
      savedErrno:保存错误码的指针
*/
ssize_t Buffer::writeFd(int fd, int* savedErrno)
{
    ssize_t n = ::write(fd,this->beginRead(),ReadBytes());

    if(n < 0)
    {
        *savedErrno = errno;
        // std::cerr << *savedErrno << std::endl;
    }
    else if(n > 0)
    {
        this->retrieve(n);
    }
    return n;
}