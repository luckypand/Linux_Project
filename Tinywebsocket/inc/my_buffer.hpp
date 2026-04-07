#pragma once
#include <cstring>   //perror
#include <iostream>
#include <string>
#include <unistd.h>  // writewd
#include <sys/uio.h> //read
#include <vector> //readv
#include <atomic>
#include <assert.h>

//|<-----prependable----><-----readable----><-----writeable---->|
//|                     r                  w                    size
class Buffer
{
public:
    Buffer(int Buffersize = 1024);
    ~Buffer() = default;

    //resource return 
    size_t WritableBytes() const;       
    size_t ReadableBytes() const ;
    size_t PrependableBytes() const;
    char* BeginWrite();    
    char* BeginRead();
    char* BeginPtr();
    std::string RetrieveAllToStr();

    //resource judgement
    void EnsureWriteable(size_t len); 

    //index move
    void HasWritten(size_t len);    

    //space change
    void MakeSpace_(size_t len);

    //data writing
    void Append(const std::string& str);
    void Append(const char* str, size_t len);
    void Append(const void* data, size_t len);
    void Append(const Buffer& buff);
    ssize_t WriteFd(int fd, int* Errno);

    //data reading
    ssize_t ReadFd(int fd, int* Errno);

private:
    std::vector<char> buffer_;  
    std::atomic<std::size_t> readPos_;  // 读的下标
    std::atomic<std::size_t> writePos_; // 写的下标    
};