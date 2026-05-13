#pragma once
#include <iostream>
#include <vector>
#include <string>

class Buffer
{
public:
    static const size_t kCheapPretendBytes = 8;
    static const size_t kInitBytes = 1024;

    explicit Buffer(size_t buffer_size = kInitBytes);
    ~Buffer() = default;

    //读接口
    const char* peek() const;          //返回可读区起始指针（const），用于零拷贝读取
    void retrieve(size_t len);         //消费 len 字节（仅移动读指针，不拷贝）
    void retrieveAll();                //消费全部可读字节
    std::string retrieveAsString();    //以 std::string 形式取出全部可读数据（拷贝）
    std::string retrieveAsString(size_t len); //取出前 len 字节为 string   

    //写接口    
    void append(const char* data, size_t len);  // 写入原始字节
    void append(const std::string& data);       // 写入 string
    const char* beginWrite() const;             //返回可写区起始指针，用于零拷贝写入（配合 hasWritten）
    void hasWritten(size_t len);                //手动推进写指针（与 beginWrite 配对） 

    //预备接口
    void prepend(const void* data, size_t len); //在可读区前面写入（不移动可读区内容） 

    //查找接口 — 协议解析
    void findCRLF();
    void findEOL(); 

    //文件描述符接口
    ssize_t readFd(int fd, int* savedErrno);   //从 fd 读取数据到 Buffer
    ssize_t writeFd(int fd, int* savedErrno);  //将 Buffer 中的可读数据写入 fd

    //返回内部数据
    size_t ReadBytes() { return WriteIndex_ - ReadIndex_ ; }
    size_t WriteBytes() { return buffer_.size() - WriteIndex_; }
    size_t PretendBytes() { return ReadIndex_; }

    //工具
    void swap(Buffer& rhs) { buffer_.swap(rhs.buffer_); std::swap(ReadIndex_, rhs.ReadIndex_); std::swap(WriteIndex_, rhs.WriteIndex_); }
    void clear() { ReadIndex_ = WriteIndex_ = kCheapPretendBytes; }

private:
    //返回地址值
    char* begin() { return &*buffer_.begin(); }
    const char* begin() const { return &*buffer_.begin(); }
    char* beginWrite() { return begin() + WriteIndex_; }   //可写区起始位置
    char* beginRead() { return begin() + ReadIndex_; }     //可读区起始位置

    //空间扩容和移动
    void ensureWriteable(size_t len);    //确保可写位置足够,复用makespace
    void makespace(size_t len);          //判断空间是否需要扩容，否则只进行搬移

    std::vector<char> buffer_;
    size_t ReadIndex_;
    size_t WriteIndex_;
};