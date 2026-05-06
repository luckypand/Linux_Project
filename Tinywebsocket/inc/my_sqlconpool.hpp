#pragma once
#include <queue>
#include <mysql/mysql.h>
#include <mutex>
#include <semaphore.h>

class Sqlconpool
{
public:
    static Sqlconpool& Instance(); //单例模式
    void Init(const char* host, int port,//单例模式初始化与申请实例分离
              const char* user,const char* pwd, 
              const char* dbName, int connSize = 10);  
    size_t GetFreeconnet();           //获得剩余MYSQL连接的数量(信号量直接拿？)
    MYSQL* Getconnect();           //获得一个MYSQL连接
    void  Freeconnect(MYSQL* con);           //释放一个连接
    void ClosePool();              //关闭连接池

private:
    Sqlconpool() = default;                  //默认构造
    Sqlconpool(const Sqlconpool&) = delete;  //禁用拷贝构造
    Sqlconpool(Sqlconpool&&) = delete;       //禁用移动构造
    Sqlconpool& operator=(const Sqlconpool&) = delete; //禁用拷贝赋值
    Sqlconpool& operator=(Sqlconpool&&) = delete;      //禁用移动赋值

    std::queue<MYSQL* > sqlconqueue_;
    std::mutex mtx_;
    sem_t semId_;
};

//使用RAII封装的原因是此时智能指针管理的对象并不是被释放了，而是被重新放入池中
//注意SqlconRAII管理的是单个连接资源而不是整个资源池 
class SqlconRAII
{
public:
    SqlconRAII(MYSQL*& connect_ptr,Sqlconpool* Sqlconpool_ptr);    //默认构造获得一个连接资源
    ~SqlconRAII();                                                    //析构释放该资源进入连接池
private:
    MYSQL* connect_;            //被管理的连接资源对象本身
    Sqlconpool* Sqlconpool_ptr_;    //指向连接池本身
};
