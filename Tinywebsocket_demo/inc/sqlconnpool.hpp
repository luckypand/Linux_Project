#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#if __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#elif __has_include(<mariadb/mysql.h>)
#include <mariadb/mysql.h>
#elif __has_include(<mysql.h>)
#include <mysql.h>
#else
#error "MySQL header not found. Please install libmysqlclient-dev or libmariadb-dev"
#endif
#include <cassert>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "../inc/log.hpp"

class SqlConnPool {
public:
    static SqlConnPool *Instance();

    MYSQL *GetConn();
    void FreeConn(MYSQL * conn);
    int GetFreeConnCount();

    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize);
    void ClosePool();

private:
    SqlConnPool() = default;
    ~SqlConnPool() { ClosePool(); }

    int MAX_CONN_;

    std::queue<MYSQL *> connQue_;
    std::mutex mtx_;
    sem_t semId_;
};


//智能指针管理的是对象的创建和释放，但是RAII类管理了资源的回收

/* 资源在对象构造初始化 资源在对象析构时释放*/
class SqlConnRAII {//连接池内部是连接线程而不是任务，因此需要归还归还数据库连接（MYSQL指针）而不是销毁
public:
    SqlConnRAII(MYSQL** sql, SqlConnPool *connpool) {
        assert(connpool);
        *sql = connpool->GetConn();
        sql_ = *sql;
        connpool_ = connpool;
    }
    
    ~SqlConnRAII() {
        if(sql_) { connpool_->FreeConn(sql_); }
    }
    
private:
    MYSQL *sql_;
    SqlConnPool* connpool_;
};

#endif // SQLCONNPOOL_H
