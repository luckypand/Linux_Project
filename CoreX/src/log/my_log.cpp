#include "my_log.hpp"
#include "my_blockqueue.hpp"
#include "my_buffer.hpp"

using namespace std;
Log::Log()
	: isOpen_(false)
	, isAsync_(false)
	, level_(0)
	, toDay_(-1)
	, lineCount_(0)
	, path_(nullptr)
	, suffix_(nullptr)
	, fp_(nullptr)
    ,mtx_() //有构造函数的成员调用默认构造
{
}

Log::~Log()
{
    //如果是异步先将deque剩余的任务处理干净
    while(isAsync_ && !deque_->empty())
    {
        flush();
    }
    if(isAsync_)//debug4.15 : 异步模式才进行关闭deque和写线程
    {
        deque_->Close();//关闭deque
        writeThread_->join();//关闭写线程        
    }
    if(fp_)//文件未关闭
    {
        lock_guard<mutex> locker(mtx_);//？为什么这里要上锁，写进程不是关闭了吗?
        flush();
        fclose(fp_);
    }
}

/*
* @brief:
*     单例的懒汉模式，通过Meyers构造方式
*/
Log* Log::Instance() //static Log*只能在类内进行声明
{   
    static Log Instance;
    return &Instance;
}

/*
* @brief:
*     通过外部函数调用异步写入函数
*/
void Log::FlushLogThread()
{
    Log::Instance()->AsyncWrite_();
}

/*
* @brief:
*     真正的线程异步写入函数，注意pop()返回true,所以每次只会处理一条日志消息
*/
void Log::AsyncWrite_()
{
    string str = "";
    while(deque_->pop(str))//异步从阻塞队列弹出消息
    {
        fputs(str.c_str(),fp_);//写入文件
    }
}

/*
* @brief:
*     刷新日志缓冲区，异步日志则唤醒消费者线程，处理掉剩下的任务；同步日志则直接清空输入缓冲区
*/
void Log::flush()
{
    if(isAsync_)//异步模式
    {
        deque_->flush();//通知消费者干活，但是不保证干完
    }
    fflush(fp_);
}

/*
* @brief:
*     接收并保存基础配置,判断日志模式,若为异步且资源未创建
*     重置运行时状态, 生成当天日志文件名,在锁保护下切换文件句柄
*     打开失败时尝试兜底并断言
*/
void Log::Init(int level,const char* path,
        int Max_capacity,const char* suffix)
{
    isOpen_ = true;
    level_ = level;
    path_ = path;
    suffix_ = suffix;

    if(Max_capacity)//有deque，则进行异步写入   
    {
        isAsync_ = true;
        if(!deque_)//如果deque为空，则使用智能指针右值转移创建
        {
            unique_ptr<Blockqueue<std::string>> newDeque
                (new Blockqueue<std::string>(Max_capacity));
            deque_ = move(newDeque);
            
            //线程挂载异步写日志函数,开始写入
            unique_ptr<thread> newThread(new thread(FlushLogThread));
            writeThread_ = move(newThread);
        }
    }
    else
    {
        isAsync_ = false;
    }
    //生成当天的日志文件名
    char fileName[LOG_NAME_LEN] = {0};
    time_t timer = time(nullptr);
    struct tm* systime = localtime(&timer);
    snprintf(fileName, LOG_NAME_LEN, "%s/%04d_%02d_%02d%s",
    path_,    // 日志文件路径
    systime->tm_year + 1900,    // 2024
    systime->tm_mon + 1,        // 04（注意+1！）
    systime->tm_mday,            // 09
    suffix_                     // 日志文件后缀
    );

    {
        lock_guard<mutex> locker(mtx_);
        //清理旧文件
        if(fp_)
        {
            flush();
            fclose(fp_);
        }
        //追加写入新的日志文件
        fp_ = fopen(fileName, "a");
        if(nullptr == fp_)
        {
            //打开失败进行兜底
            mkdir(path_, 0777);
            fp_ = fopen(fileName, "a");
            assert(fp_ != nullptr);
        }           
    }
}

/*
* @brief:
*     写入日志级别
*/                    
void Log::AppendLogLevelTitle_(int level)
{
    switch(level) {
    case 0:
        buff_.Append("[debug]: ", 9);
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}

/*
* @brief:
*     根据日志级别和格式化字符串生成日志内容，异步模式则将日志消息加入阻塞队列
*   等待写线程处理；同步模式则直接写入文件
*/
void Log::write(int level, const char *format,...)
{
    //先确定要写入日志的文件名
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    va_list vaList;//配合可变参数使用

    unique_lock<mutex> locker(mtx_);//锁保护日志文件切换
    //判断日志写入时间，日志是否超过最大行数，满足条件则切换日志文件
    if(toDay_ != t.tm_mday || (lineCount_ && (lineCount_ % MAX_LINES == 0)))
    {
        char newFile[LOG_NAME_LEN]; //新日志文件名
        char tail[36] = {0}; //日期后缀
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        if(toDay_ != t.tm_mday) //日期不匹配，新建日期的log文件
        {
            snprintf(newFile,LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            toDay_ = t.tm_mday;
            lineCount_ = 0;
        }   
        else    //日期匹配，行数超了，新建文件
        {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lineCount_ / MAX_LINES), suffix_);
        }
        //刷新原本的文件，防止丢失日志，关闭原本的文件，打开新文件
        flush();
        fclose(fp_);
        fp_ = fopen(newFile, "a");
        assert(fp_ != nullptr);
    }
    locker.unlock();//解锁

    //格式化日志内容
    {
       unique_lock<mutex> locker(mtx_);//出作用域默认解锁
       //向buff_写入
       snprintf(buff_.BeginWrite(),buff_.WritableBytes(),"%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
       AppendLogLevelTitle_(level);//写入日志级别,默认缓冲区下标已写
       va_start(vaList, format);//其他要写入的变量来自...可变参数
       int n = vsnprintf(buff_.BeginWrite(),buff_.WritableBytes(),format,vaList);        
       va_end(vaList);
       buff_.HasWritten(n);//更新缓冲区下标
       buff_.Append("\n\0", 2);//写入换行符和字符串,\0是为了保证字符串正确结束
       //判断同步异步
       if(isAsync_ && deque_ && !deque_->full()) //异步且未满
       {
            deque_->push_back(buff_.RetrieveAllToStr());//写入阻塞队列
       }
       else//同步方式进行写入log文件
       {
            fputs(buff_.BeginRead(),fp_);
       }
    }
    buff_.RetrieveAllToStr();//buffer写入后清空
}

int Log::Isopen()
{
    return isOpen_;
}

int Log::GetLevel()
{
    return level_;
}