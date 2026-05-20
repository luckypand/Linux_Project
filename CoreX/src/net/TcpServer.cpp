#include "TcpServer.hpp"

/*
* @brief:
*     构造TcpServer，设置acceptor_的连接函数
  @param:
        loop	IO事件循环核心
        inport	监听IP地址
        port	监听端口
        name	服务器名称       
*/
TcpServer::TcpServer(EventLoop* loop,const std::string& inport,uint16_t port,const std::string& name)
    :loop_(loop)
    ,inport_(inport + ":" + std::to_string(port))
    ,name_(name)
    ,acceptor_(std::make_unique<Acceptor>(loop,port))
    ,threadPool_(std::make_shared<EventLoopThreadPool>(loop,name))
{
    //开启acceptor,但先不启动线程池
    acceptor_->setNewConnectionCallback(
        [this](int sockfd, const std::string& peerAddr){
            this->newConnection(sockfd,peerAddr);
        }   
    );
}

/*
* @brief:
*     析构TcpServer
*/
TcpServer::~TcpServer()
{
    //判断主Reactor是否在本线程，调用线程对应loop挨个释放线程池的连接
    loop_->assertInLoopThread();
    for(auto& conn : connections_)
    {
        //conn.second是shared_ptr,reset删除一个second是shared_ptr的引用计数
        auto it = conn.second;
        conn.second.reset();
        //调用Tcpconnection中的销毁连接
        //it是TcpConnection的shared_ptr
        it->getLoop()->RunInloop(
            [it](){
                it->connectDestroyed();
            }   
        );
    }
}

/*
* @brief:
*     回调连接实际调用的函数
*/
void TcpServer::newConnection(int sockfd, const std::string& peerAddr)
{
    loop_->assertInLoopThread();
    //取线程池轮询的一个子Reactor
    EventLoop * subloop = threadPool_->chooseNextLoop();
    //生成唯一的连接名
    char buff[64] = {0};
    snprintf(buff,sizeof(buff),"%s-%d",inport_.c_str(),nextConnectId - 1);
    nextConnectId++;
    std::string connectName = name_ + buff;
    //实例化一个Tcpconnection，绑定到对应loop上，初始化其回调
    TcpConnectionPtr conn = std::make_shared<TcpConnection>(loop_,sockfd);
    connections_[connectName] = conn;
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setwriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(
        [this,conn,connectName](const TcpConnectionPtr&){
            this->removeConnection(conn,connectName);
        }
    );

    //跨线程通知子loop去epoll中注册自己的新连接读事件
    subloop->RunInloop(
        [conn](){
            conn->connectEstablished();
        }
    );
}

/*
* @brief:
*     提供给TcpServer内部CloseCallback回调实际调用的函数
*/
void TcpServer::removeConnection(const TcpConnectionPtr& conn,const std::string& connectName)
{
    //主Reactor让目标Reactor在自己的loop中执行移除connection
    loop_->RunInloop(
        [this,conn,connectName](){
            this->removeConnectionInLoop(conn,connectName);
        }
    );
}

/*
* @brief:
*     提供给removeConnection调用，使得Reactor能在自己的Loop内移除连接
*/
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn,const std::string& connectName)
{
    //判断Loop是否为本线程，移除TcpServer中的哈希表，通知对应的
    //Reactor将销毁任务放入队列排队
    loop_->assertInLoopThread();
    size_t n = connections_.erase(connectName); //记录剩余引用计数

    EventLoop* subloop = conn->getLoop();
    subloop->SengToPending(
        [conn](){
            conn->connectDestroyed();
        }
    );
}

/*
* @brief:
*     设置子线程的数量
*/
void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setNumThreads(numThreads);
}

/*
* @brief:
*     初次启动
*/
void TcpServer::start()
{
    if(started++ == 0)//第一次启动
    {
        threadPool_->start();//启动线程池
        //acceptor_->listen(); //开始监听
        loop_->RunInloop(
            [this]()
            {
                acceptor_->listen();
            }
        );
    }
}