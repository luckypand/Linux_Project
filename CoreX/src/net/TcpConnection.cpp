#include "TcpConnection.hpp"

TcpConnection::TcpConnection(EventLoop* loop,int sockfd)
    :loop_(loop)
    ,state_(kConnencting)
    ,socket_(std::make_unique<Socket>(sockfd))
    ,channel_(std::make_unique<Channel>(loop,sockfd))
{
    channel_->setErrorCallback([this](){this->handleError();});
    channel_->setReadCallback([this](){this->handleRead();});
    channel_->setWriteCallback([this](){this->handleWrite();});
    channel_->setCloseCallback([this](){this->handleClose();});
}

TcpConnection::~TcpConnection()
{
    //自动调用析构
}

/*
* @brief:
*     被TcpServer 调用，正式确立连接
*/
void TcpConnection::connectEstablished()
{
    //状态机改变，channel开始关注读fd事件，通知业务层执行连接回调
    state_ = kConnected;
    channel_->enableReading();
    connectionCallback_(shared_from_this());
}

/*
* @brief:
*     被TcpServer 调用，正式销毁连接
*/
// void TcpConnection::connectDestroyed()
// {

    // loop_->assertInLoopThread();
    // if (state_ == kConnected)
    // {
    //     setState(kDisconnected);
    //     channel_->disableAll();

    //     connectionCallback_(shared_from_this());
    // }
    // channel_->remove();
// }

/*
* @brief:
*     处理用户缓冲层面的读事件
*/
void TcpConnection::handleRead()
{
    //将文件内容读至inBuffer中，依据结果进行分类
    int savedErrno = 0;

    ssize_t n = inBuffer.readFd(socket_->fd(),&savedErrno);
    if(n > 0)//读成功(业务处理)
    {
        messageCallback_(shared_from_this(),inBuffer);
    }
    else if(n == 0)//连接关闭(网络库处理)   
    {
        handleClose();
    }
    else//连接出错(网络库处理)   
    {
        handleError();
    }
}

/*
* @brief:
*     发送数据(应用层调用)
*/
void TcpConnection::send(const std::string& message)
{
    if(kConnected == state_)
    {
        if(loop_->IsInloopthread())//减少lamda构造和指针传递
        {
            sendInLoop(message);
        }
        else
        {
            loop_->RunInloop(
                [ptr = shared_from_this(),message]
                (){ptr->sendInLoop(message);});
        }
    }
}

/*
* @brief:
*     发送数据(内核调用)
*/
void TcpConnection::sendInLoop(const std::string& message)
{
    ssize_t nwrote = 0;

    //之前没关注过写事件且要发送的内容为空
    if(!channel_->IsWriting() && outBuffer.ReadBytes() == 0)
    {
        nwrote = ::write(socket_->fd(),message.data(),message.size());
        if(nwrote < 0)
        {
            nwrote = 0;
        }
    }
    //发现没写完
    if(static_cast<size_t>(nwrote) < message.size())
    {
        //先存在outBuffer中等待内核空再接着写
        outBuffer.append(message.data() + nwrote,message.size() - nwrote);
        //让用户开始关注可写(此时不会出现疯狂轮询，因为刚才内核输出缓存被写入，只有空出来才会提醒)
        if(!channel_->IsWriting())
        {
            channel_->enableWriting();
        }
    }
}

/*
* @brief:
*     处理写事件(内核调用)
*/
void TcpConnection::handleWrite()
{
    //判断如果此时用户关注写事件，开始写剩余
    if(channel_->IsWriting())
    {
        ssize_t n = ::write(socket_->fd(),outBuffer.peek(),outBuffer.ReadBytes());
        if(n > 0)
        {
            //判断是否写完，如果写完了，不再关注写事件，同时根据连接状态判断要不要关闭
            if(outBuffer.ReadBytes() == 0)
            {
                channel_->disableall();
                if(state_ == kDisconnecting)
                {
                    shutdown();
                }
            }
        }
    }
}

/*
* @brief:
*     TcpConnection处理连接关闭(内核调用),区分closeCallback_，
后者是通知上层来处理TcpConnection
*/
void TcpConnection::handleClose()
{
    state_ = kDisconnected;
    channel_->disableall();
    closeCallback_(shared_from_this());
}

/*
* @brief:
*     TcpConnection处理关闭
*/
void TcpConnection::shutdown()
{
    if(state_ == kDisconnected)
    {
        state_ = kDisconnecting;
        loop_->RunInloop(
            [this](){
                if(!channel_->IsWriting())
                {
                    ::shutdown(socket_->fd(),SHUT_WR);
                }
            }
        );
    }
}

/*
* @brief:
*     被TcpServer 调用，正式销毁连接
*/
void TcpConnection::connectDestroyed()
{
    loop_->assertInLoopThread();
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableall();

        connectionCallback_(shared_from_this());
    }
    loop_->RemoveChannel(channel_.get());
}