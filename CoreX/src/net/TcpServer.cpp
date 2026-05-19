#include "TcpServer.hpp"

/*
* @brief:
*     构造TcpServer，设置acceptor_的连接函数
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
        conn.second->
    }
}

/*
* @brief:
*     回调连接实际调用的函数
*/
void TcpServer::newConnection(int sockfd, const std::string& peerAddr)
{

}