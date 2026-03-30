#pragma once

#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <iostream>

#define INFO 0     // 常规信息
#define DEBUG 1    // debug信息
#define WARNING 2  // 警告信息
#define ERROR 3    // 错误信息
#define FATAL 4    // 致命错误信息

#define SCREEN 1    // 向显示器文件写入
#define ONEFILE 2   // 向单个文件进行写入
#define CLASSIFY 3  // 分类将日志进行写入

#define LOG_BUFFER_SIZE 1024  // 日志所需大小

#define LogFile "log.txt"  // 默认日志文件文件名

class Log {
 public:
  Log() {  // 构造函数 用于初始化默认打印方式以及设置默认路径
    printMethod = SCREEN;
    path = "./log/";
  }

  ~Log() { ; }  // 析构函数 无用

  void Enable(int method) { printMethod = method; }  // 更改日志输出方式

  const std::string levelToString(int level) {  // 以字符串形式获取日志等级
    switch (level) {
      case INFO:
        return "INFO";
      case DEBUG:
        return "DEBUG";
      case WARNING:
        return "WARNING";
      case ERROR:
        return "ERROR";
      case FATAL:
        return "FATAL";
      default:
        return "NONE";
    }
  }

  // void logmessage(int level, const char* format, ...) {
  //   // 获取时间
  //   time_t currentTime = time(nullptr);
  //   struct tm* ctime = localtime(&currentTime);
  // printf("%d-%d-%d %d:%d:%d\n", ctime->tm_year + 1900, ctime->tm_mon + 1,
  //        ctime->tm_mday, ctime->tm_hour, ctime->tm_min, ctime->tm_sec);
  // //        //打印 debug

  //   // 格式: 默认部分 + 自定义部分
  //   // 默认部分
  //   char defaultbuffer[LOG_BUFFER_SIZE];

  //   snprintf(defaultbuffer, sizeof(defaultbuffer), "[%s][%d-%d-%d %d:%d:%d]",
  //            levelToString(level).c_str(), ctime->tm_year + 1900,
  //            ctime->tm_mon + 1, ctime->tm_mday, ctime->tm_hour,
  //            ctime->tm_min, ctime->tm_sec);

  //   // 自定义部分
    // va_list s;
    // va_start(s, format);
    // char custombuffer[LOG_BUFFER_SIZE];
    // vsnprintf(custombuffer, sizeof(custombuffer), format, s);
    // va_end(s);

  //   // 组合
  //   char logtxt[LOG_BUFFER_SIZE * 2];
  //   snprintf(logtxt, sizeof(logtxt), "%s %s\n", defaultbuffer, custombuffer);

  //   // 组合完毕后可直接打印也可直接写到文件中
  //   // std::cout << logtxt << std::endl;

  //   printLog(level, logtxt);
  // }

  void printLog(int level, const std::string& logtxt) {  // 不同方式输出日志
    switch (printMethod) {
      case SCREEN:
        /* code */
        std::cout << logtxt << std::endl;
        break;
      case ONEFILE:
        printOneFile(LogFile, logtxt);
        break;
      case CLASSIFY:
        printClassify(level, logtxt);
        break;
      default:
        break;
    }
  }

  void printOneFile(const std::string& logname,
                    const std::string& logtxt) {  // 单个文件输出日志
    std::string _logname = path + logname;
    int fd = open(_logname.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0666);
    if (fd < 0) {
      std::cerr << "open fail" << std::endl;
      return;
    }
    write(fd, logtxt.c_str(), logtxt.size());
    close(fd);
  }

  void printClassify(int level,
                     const std::string& logtxt) {  // 分类形式输出日志
    std::string filename = LogFile;
    filename += ".";
    filename += levelToString(level);
    printOneFile(filename, logtxt);
  }

  void operator()(int level, const char* format,
                  ...) {  // 重载()操作符并将原本接口进行封装
    // 获取时间
    time_t currentTime = time(nullptr);
    struct tm* ctime = localtime(&currentTime);
    // printf("%d-%d-%d %d:%d:%d\n", ctime->tm_year + 1900, ctime->tm_mon + 1,
    //        ctime->tm_mday, ctime->tm_hour, ctime->tm_min, ctime->tm_sec);
    //        //打印 debug

    // 格式: 默认部分 + 自定义部分
    // 默认部分
    char defaultbuffer[LOG_BUFFER_SIZE];

    snprintf(defaultbuffer, sizeof(defaultbuffer),
             "[%s][%d-%02d-%02d %02d:%02d:%02d]", levelToString(level).c_str(),
             ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday,
             ctime->tm_hour, ctime->tm_min, ctime->tm_sec);

    // 自定义部分
    va_list s;
    va_start(s, format);
    char custombuffer[LOG_BUFFER_SIZE];
    vsnprintf(custombuffer, sizeof(custombuffer), format, s);
    va_end(s);

    // 组合
    char logtxt[LOG_BUFFER_SIZE * 2];
    snprintf(logtxt, sizeof(logtxt), "%s %s\n", defaultbuffer, custombuffer);

    // 组合完毕后可直接打印也可直接写到文件中
    // std::cout << logtxt << std::endl;

    printLog(level, logtxt);
  }

 private:
  int printMethod;   // 打印风格
  std::string path;  // 默认路径
};


// -------------------

// void sum(int n, ...) {
//   va_list s;  // va_list 即char* 提取可变参数中的参数
//   //从右向左进行实例化 所以需要先实例化可变部分
//   // 虽然压栈从右往左压 但实际上的计算还是从左往右计算
//   // 所以第一个参数还是第一个参数 用来计算出第一个参数的具体位置
//   va_start(s, n);
//   int sum = 0;
//   while (n--) {
//     sum += va_arg(s, int);
//   }
//   va_end(s);
//   std::cout << sum << std::endl;
// }