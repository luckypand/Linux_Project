#include "my_log.hpp"
#include "my_blockqueue.hpp"
#include "my_buffer.hpp"

using namespace std;
Log::Log()
	: isOpen_(false)
	, isAsync_(false)
	, level_(0)
	, basePath_()
	, suffix_(nullptr)
	, buff_()
    ,mtx_()
{
}

Log::~Log()
{
    //如果是异步先将deque剩余的任务处理干净
    while(isAsync_ && deque_ && !deque_->empty())
    {
        flush();
    }
    if(isAsync_)//debug4.15 : 异步模式才进行关闭deque和写线程
    {
        deque_->Close();//关闭deque
        writeThread_->join();//关闭写线程        
    }
    //关闭所有模块的日志文件
    for(auto& kv : moduleFiles_)
    {
        if(kv.second)
        {
            fflush(kv.second);
            fclose(kv.second);
        }
    }
    moduleFiles_.clear();
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
*     按模块名将日志写入对应的日志文件
*/
void Log::AsyncWrite_()
{
    LogEntry entry;
    while(deque_->pop(entry))//异步从阻塞队列弹出消息
    {
        FILE* fp = GetOrCreateModuleFile_(entry.module);
        if(fp)
        {
            fputs(entry.message.c_str(), fp);//写入对应模块的文件
        }
    }
}

/*
* @brief:
*     刷新日志缓冲区，异步日志则唤醒消费者线程；同步日志则刷新所有模块的文件
*/
void Log::flush()
{
    if(isAsync_)//异步模式
    {
        deque_->flush();//通知消费者干活，但是不保证干完
    }
    // 刷新所有模块文件
    for(auto& kv : moduleFiles_)
    {
        if(kv.second)
        {
            fflush(kv.second);
        }
    }
}

/*
* @brief:
*     从 __FILE__ 中提取模块名
*     例如 "src/net/EventLoop.cpp" → "net"
*           "src/log/my_log.cpp"    → "log"
*           "tests/test.cpp"        → "root"（不在 src/ 下则归入 root）
*/
std::string Log::ExtractModule_(const char* file)
{
    std::string path(file);
    auto pos = path.find("src/");
    if(pos == std::string::npos)
        return "root"; // 不在 src/ 下的文件归入 root 模块

    pos += 4; // 跳过 "src/"
    auto end = path.find('/', pos);
    if(end == std::string::npos)
        return "root";

    return path.substr(pos, end - pos);
}

/*
* @brief:
*     获取（或创建）指定模块的日志文件句柄
*     自动处理目录创建、日期轮转、行数轮转
*/
FILE* Log::GetOrCreateModuleFile_(const std::string& module)
{
    // 检查是否已有该模块的句柄
    auto it = moduleFiles_.find(module);
    if(it != moduleFiles_.end() && it->second != nullptr)
    {
        // 检查是否需要轮转
        time_t timer = time(nullptr);
        struct tm* systime = localtime(&timer);
        int today = systime->tm_mday;

        int& curDay = moduleToDay_[module];
        int& curLine = moduleLineCount_[module];

        if(curDay != today || (curLine > 0 && curLine % MAX_LINES == 0))
        {
            // 需要轮转：关闭旧文件，重新创建
            fflush(it->second);
            fclose(it->second);
            moduleFiles_.erase(module);
            // 不直接 return，继续往下创建新文件
        }
        else
        {
            return it->second;
        }
    }

    // 创建新文件
    std::string modulePath = basePath_ + "/" + module;
    mkdir(modulePath.c_str(), 0777);

    char fileName[LOG_NAME_LEN] = {0};
    time_t timer = time(nullptr);
    struct tm* systime = localtime(&timer);
    int today = systime->tm_mday;

    if(moduleToDay_[module] != today)//日期不同则新建日期文件
    {
        snprintf(fileName, LOG_NAME_LEN, "%s/%04d_%02d_%02d%s",
            modulePath.c_str(),
            systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday,
            suffix_);
        moduleToDay_[module] = today;
        moduleLineCount_[module] = 0;
    }
    else//日期相同但行数超限，新建分卷文件
    {
        snprintf(fileName, LOG_NAME_LEN, "%s/%04d_%02d_%02d-%d%s",
            modulePath.c_str(),
            systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday,
            moduleLineCount_[module] / MAX_LINES,
            suffix_);
    }

    FILE* fp = fopen(fileName, "a");
    if(nullptr == fp)
    {
        mkdir(modulePath.c_str(), 0777);//兜底
        fp = fopen(fileName, "a");
        assert(fp != nullptr);
    }

    moduleFiles_[module] = fp;
    return fp;
}

/*
* @brief:
*     接收并保存基础配置,判断日志模式
*     异步模式则创建阻塞队列和后台写线程
*     （注意：不再在 Init 时创建具体日志文件，
*      改为在 write/AsyncWrite_ 时按模块按需创建）
*/
void Log::Init(int level, const char* basePath,
        int Max_capacity, const char* suffix)
{
    isOpen_ = true;
    level_ = level;
    basePath_ = basePath;
    suffix_ = suffix;

    if(Max_capacity)//有deque，则进行异步写入   
    {
        isAsync_ = true;
        if(!deque_)//如果deque为空，则使用智能指针右值转移创建
        {
            deque_ = make_unique<Blockqueue<LogEntry>>(Max_capacity);
            
            //线程挂载异步写日志函数,开始写入
            writeThread_ = make_unique<thread>(FlushLogThread);
        }
    }
    else
    {
        isAsync_ = false;
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
*   等待写线程处理；同步模式则直接写入对应的模块文件
*   参数 file 由 LOG 宏自动传入 __FILE__
*/
void Log::write(const char* file, int level, const char *format,...)
{
    // 从 __FILE__ 提取模块名（如 "net"、"log"、"ipc" 等）
    std::string module = ExtractModule_(file);

    //先确定要写入日志的时间
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    va_list vaList;//配合可变参数使用

    unique_lock<mutex> locker(mtx_);//锁保护
    //判断当前模块是否需要轮转日志文件
    {
        int& curDay = moduleToDay_[module];
        int& curLine = moduleLineCount_[module];

        // 如果已有文件句柄，检查是否需要轮转
        auto it = moduleFiles_.find(module);
        if(it != moduleFiles_.end() && it->second != nullptr)
        {
            if(curDay != t.tm_mday || (curLine && (curLine % MAX_LINES == 0)))
            {
                fflush(it->second);
                fclose(it->second);
                moduleFiles_.erase(module);
            }
        }
        // 行数递增（在写入前递增，配合 GetOrCreateModuleFile_ 中的轮转判断）
        curLine++;
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
            deque_->push_back({std::move(module), buff_.RetrieveAllToStr()});//写入阻塞队列
       }
       else//同步方式直接写入对应模块的log文件
       {
            FILE* fp = GetOrCreateModuleFile_(module);
            if(fp)
            {
                fputs(buff_.BeginRead(), fp);
            }
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