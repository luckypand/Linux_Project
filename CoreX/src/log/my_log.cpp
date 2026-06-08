#include "my_log.hpp"
#include "my_blockqueue.hpp"
#include "my_buffer.hpp"
#include <dirent.h>
#include <cstring>

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
*     关键设计：pop() 释放 Blockqueue::mtx_ 后，再获取 Log::mtx_ 来安全访问 moduleFiles_
*     锁顺序：Blockqueue::mtx_ → Log::mtx_（与 write() 的 mtx_ → push 相反但永不重叠，避免 ABBA）
*/
void Log::AsyncWrite_()
{
    LogEntry entry;
    while(deque_->pop(entry))//异步从阻塞队列弹出消息（内部持有 Blockqueue::mtx_，返回后释放）
    {
        unique_lock<mutex> locker(mtx_);      // 保护 moduleFiles_ 等共享状态
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
    // 刷新所有模块文件（需加锁，与 AsyncWrite_/write 中的 moduleFiles_ 访问互斥）
    unique_lock<mutex> locker(mtx_);
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
*     扫描模块日志目录，找到当天已有日志文件的最大序号
*     用于跨运行的文件序号递增，使每次程序启动都创建新的日志文件
*     文件名格式: YYYY_MM_DD-N.log，提取最大的 N
* @return: 当天已有文件的最大序号（0 表示尚无文件）
*/
int Log::ScanInitSeq_(const std::string& modulePath)
{
    int maxSeq = 0;
    DIR* dir = opendir(modulePath.c_str());
    if (!dir) return 0;

    // 构造当天日期前缀 "YYYY_MM_DD-"
    time_t timer = time(nullptr);
    struct tm* systime = localtime(&timer);
    char todayPrefix[32];
    snprintf(todayPrefix, sizeof(todayPrefix), "%04d_%02d_%02d-",
             systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday);

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        // 匹配 "YYYY_MM_DD-N.log" 或 "YYYY_MM_DD-N" 格式
        int prefixLen = strlen(todayPrefix);
        if (strncmp(entry->d_name, todayPrefix, prefixLen) == 0)
        {
            int seq = atoi(entry->d_name + prefixLen);
            if (seq > maxSeq) maxSeq = seq;
        }
    }
    closedir(dir);
    return maxSeq;
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
            if(curDay != today)
            {
                // 日期变更 → 重置序号（下次创建文件时 ScanInitSeq_ 重新扫描）
                moduleFileSeq_.erase(module);
            }
            else
            {
                // 同日行数超限 → 序号递增
                moduleFileSeq_[module]++;
            }
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

    // 首次为该模块创建文件，或日期变更后 → 扫描目录确定起始序号
    if(moduleFileSeq_.find(module) == moduleFileSeq_.end())
    {
        moduleFileSeq_[module] = ScanInitSeq_(modulePath) + 1;
        moduleLineCount_[module] = 0;
    }

    // 统一按 YYYY_MM_DD-N.log 格式命名（不再有无后缀的基础文件）
    snprintf(fileName, LOG_NAME_LEN, "%s/%04d_%02d_%02d-%d%s",
        modulePath.c_str(),
        systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday,
        moduleFileSeq_[module],
        suffix_);

    moduleToDay_[module] = today;

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
*     根据日志级别和格式化字符串生成日志内容
*     异步模式：格式化后释放 mtx_，再 push 到阻塞队列（避免与 AsyncWrite_ 形成 ABBA）
*     同步模式：持 mtx_ 直接写入对应的模块文件
*   参数 file 由 LOG 宏自动传入 __FILE__
*
*   锁顺序设计（异步路径）：
*     write():       lock(mtx_) → format → unlock → push_back(Blockqueue::mtx_)
*     AsyncWrite_(): pop(Blockqueue::mtx_) → lock(mtx_) → fputs → unlock
*     两把锁永远不会被同一个线程同时持有 → 零死锁风险
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
    va_list vaList;

    // 异步模式下，格式化完成后将消息暂存于此，释放 mtx_ 后再 push
    std::string asyncMessage;
    bool shouldPushAsync = false;

    {
        unique_lock<mutex> locker(mtx_);

        // ---- Phase 1: 文件轮转检查 ----
        int& curDay = moduleToDay_[module];
        int& curLine = moduleLineCount_[module];
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
        curLine++;

        // ---- Phase 2: 格式化日志消息到 buff_ ----
        buff_.RetrieveAllToStr();  // 先清空缓冲区
        int nTime = snprintf(buff_.BeginWrite(), buff_.WritableBytes(),
                 "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
        buff_.HasWritten(nTime);   // 推进写指针，避免被后续 Append 覆盖
        AppendLogLevelTitle_(level);
        va_start(vaList, format);
        int nMsg = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList);
        va_end(vaList);
        buff_.HasWritten(nMsg);
        buff_.Append("\n\0", 2);

        // ---- Phase 3: 分发 ----
        if(isAsync_ && deque_)
        {
            // 异步路径：提取消息到栈变量，标记待 push（不在此处 push，避免持 mtx_ 等 Blockqueue）
            asyncMessage = buff_.RetrieveAllToStr();
            shouldPushAsync = true;
        }
        else
        {
            // 同步路径：直接持锁写入文件
            FILE* fp = GetOrCreateModuleFile_(module);
            if(fp)
            {
                fputs(buff_.BeginRead(), fp);
            }
            buff_.RetrieveAllToStr();  // 清空缓冲区
        }
    }
    // mtx_ 在此释放 —— 后续异步 push 不持 Log::mtx_

    // 异步写入阻塞队列 —— 此时不持有 mtx_，与 AsyncWrite_ 不形成 ABBA
    if(shouldPushAsync)
    {
        deque_->push_back({std::move(module), std::move(asyncMessage)});
    }
}

int Log::Isopen()
{
    return isOpen_;
}

int Log::GetLevel()
{
    return level_;
}