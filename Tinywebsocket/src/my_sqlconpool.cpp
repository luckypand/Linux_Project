//连接池区分于线程池，选择信号量作为区分标准，因为本质上是抢占固定量的资源，而不是像
//线程池一样分配任务管理任务池
#include "my_sqlconpool.hpp"
#include "my_log.hpp"

/*
* @brief:
*     单例的懒汉模式，通过Meyers构造方式
*/
Sqlconpool& Sqlconpool::Instance()
{
    static Sqlconpool sqlconpool;
    return sqlconpool;
}

/*
* @brief:
*     初始化连接池，包括连接池内部和信号量
*/
void Sqlconpool::Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize)
{
    assert(connSize > 0);
    mysql_library_init(0, nullptr, nullptr);
    //按connSize大小初始化连接池的连接
    for(int i = 0;i < connSize;i++)
    {
        MYSQL* connect = nullptr;
        connect = mysql_init(connect);
        if(!connect)
        {
            LOG_ERROR("mysql_init is error");
            assert(connect);
        }
        connect = mysql_real_connect(connect, host, user, pwd, dbName, port, nullptr, 0);
        if(!connect)
        {
            LOG_ERROR("mysql_real_connect is error");
            assert(connect);
        }
        sqlconqueue_.push(connect);
    }
    sem_init(&semId_,0,connSize); //初始化信号量
}

/*
* @brief:
*     获取连接池内部资源
*/
MYSQL* Sqlconpool::Getconnect()
{
    MYSQL* cur_connect = nullptr;
    if(sqlconqueue_.empty())
    {
        LOG_WARN("sqlconqueue is empty !!");
        return nullptr;
    }
    //取出一个连接，信号量-1,本身是原子操作不需要加锁
    sem_wait(&semId_);
    //访问临界区
    {
        std::lock_guard<mutex> locker(mtx_);      
        cur_connect = sqlconqueue_.front();
        sqlconqueue_.pop();
    }
    return cur_connect;
}

/*
* @brief:
*     获取连接池内部剩余数量
*/
size_t Sqlconpool::GetFreeconnet()
{
    std::lock_guard<mutex> locker(mtx_);
    return sqlconqueue_.size();
}

/*
* @brief:
*     释放资源到连接池内部
*/
void  Sqlconpool::Freeconnect(MYSQL* con)           //释放一个连接
{
    assert(con);
    std::lock_guard<mutex> locker(mtx_);
    sqlconqueue_.push(con);
    sem_post(&semId_);
}

/*
* @brief:
*     关闭连接池
*/
void Sqlconpool::ClosePool()
{
    std::lock_guard<mutex> locker(mtx_);
    while(!sqlconqueue_.empty())
    {
        auto conn = sqlconqueue_.front();
        sqlconqueue_.pop();
        mysql_close(conn);
    }
    //MYSQL库定义的清理库资源的函数
    mysql_library_end();
}

/*
* @brief:
*     取出一个连接资源进行RAII作为初始化
*/
SqlconRAII::SqlconRAII(MYSQL*& connect_ptr,Sqlconpool* Sqlconpool_ptr)
{   
    assert(Sqlconpool_ptr);
    connect_ptr = Sqlconpool_ptr->Getconnect();
    Sqlconpool_ptr_ = Sqlconpool_ptr;
    connect_ = connect_ptr;
    assert(connect_);
}

/*
* @brief:
*     释放资源回到队列中作为RAII的析构
*/
SqlconRAII::~SqlconRAII()
{
    assert(connect_);
    Sqlconpool_ptr_->Freeconnect(connect_);
}