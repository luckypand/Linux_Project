#pragma once

#include <iostream>
#include <string>
#include <mutex>
#include <thread>
#include <memory>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <stdarg.h>
#include "my_blockqueue.hpp"
#include "my_buffer.hpp"

/*
* @brief:
*   单例模式创建log实例，提供一个全局访问点，保证整个程序中只有一个log实例
*   日志级别：DEBUG < INFO < WARN < ERROR < FATAL
*   日志文件按日期区分，超过最大行数则新建文件
*   异步日志：使用阻塞队列和独立线程实现异步日志写入，生产者线程将日志消息放入队列，消费者线程从队列中取出日志消息并写入文件，减少日志写入对主线程的影响，提高性能
*   日志输出格式：包含时间戳、日志级别、线程ID、文件名、行号等信息，方便调试和问题定位
*   日志文件管理：提供日志文件的自动切割和清理机制，防止日志文件过大或过多占用磁盘空间
*   线程安全：使用互斥锁和条件变量确保多线程环境下日志操作的安全性，避免数据竞争和死锁
*   日志级别控制：允许用户设置日志级别，只有高于或等于设定级别的日志才会被记录，减少不必要的日志输出，提高性能
*   日志格式化：提供灵活的日志格式化功能，支持自定义日志输出格式，满足不同用户的需求
*   日志输出目的地：支持将日志输出到控制台、文件或远程服务器，满足不同场景下的日志需求
*   日志性能优化：通过使用异步日志和高效的日志格式化算法，优化日志写入性能，减少对应用程序性能的影响
*   日志扩展性：设计日志系统时考虑未来的扩展性，允许用户自定义日志级别、输出格式和目的地，满足不同用户的需求
*   日志配置：提供灵活的日志配置选项，允许用户通过配置文件或代码设置日志级别、输出格式和目的地，方便用户根据需求进行调整  
*/
enum
{
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
};

class Log
{
public:
    static Log* Instance(); 

    // 初始化日志实例（阻塞队列最大容量、日志保存路径、日志文件后缀）
    void Init(int level,const char* path = "../log",
        int Max_capacity = 1024,const char* suffix = ".log");
    
    static void FlushLogThread();   // 异步写日志公有方法，调用私有方法asyncWrite
    void flush();                   //刷新阻塞队列消息到log文件
    void write(int level, const char *format,...);  // 将输出内容按照标准格式整理,写入阻塞队列
    void AppendLogLevelTitle_(int level);           // 添加日志等级
    int Isopen();                                  // 判断日志是否开启
    int GetLevel();                                  // 判断日志是否开启

private:
    //class
    Log();  //单例模式
    ~Log();

    void AsyncWrite_(); // 异步写日志方法

    static const int LOG_PATH_LEN = 256;    // 日志文件最长文件名
    static const int LOG_NAME_LEN = 256;    // 日志最长名字
    static const int MAX_LINES = 50000;     // 日志文件内的最长日志条数
    
    bool isOpen_;               //？
    bool isAsync_;              // 是否开启异步日志
    int level_;                 //日志等级
    int toDay_;                 //按当天日期区分文件
    int lineCount_;             //日志行数记录
    const char* path_;          //路径名
    const char* suffix_;        //后缀名
    Buffer buff_;               //输出的内容，缓冲区

    FILE* fp_;                                          //打开log的文件指针
    std::unique_ptr<Blockqueue<std::string>> deque_;    //阻塞队列
    std::unique_ptr<std::thread> writeThread_;          //写线程的指针
    std::mutex mtx_;                                    //同步日志必需的互斥量
};

//用户调用逻辑,注意调用宏时默认用户已经创建实例使用了init()
#define Log_Base(level,format,...)\
    do{\
        Log* log = Log::Instance();\
        if(log->Isopen() && log->GetLevel() <= level)\
        {\
            log->write(level,format,##__VA_ARGS__);\
            log->flush();\
        }\
    }while(0)\

#define LOG_DEBUG(level,format,...)  Log_Base(LOG_DEBUG,format,##__VA_ARGS__)
#define LOG_INFO(level,format,...)   Log_Base(LOG_INFO,format,##__VA_ARGS__)
#define LOG_WARN(level,format,...)   Log_Base(LOG_WARN,format,##__VA_ARGS__)
#define LOG_ERROR(level,format,...)  Log_Base(LOG_ERROR,format,##__VA_ARGS__)
#define LOG_FATAL(level,format,...)  Log_Base(LOG_FATAL,format,##__VA_ARGS__)