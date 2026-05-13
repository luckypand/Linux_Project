#include "HeapTimer.hpp"
#include <assert.h>

HeapTimer::HeapTimer()
    :TimeHeap_()
    ,ref_()
{
    TimeHeap_.reserve(64);   //预先开辟空间,而不是直接初始化TimeHeap_(64)
}

HeapTimer::~HeapTimer()
{
    clear();
}

/*
* @brief:
*     交换堆结点
*/
void HeapTimer::swapnode(size_t i,size_t j)
{
    //先交换在heap下的位置，再交换在unordered_map下对应的坐标
    assert(i < TimeHeap_.size() && j < TimeHeap_.size());
    std::swap(TimeHeap_[i],TimeHeap_[j]);
    ref_[TimeHeap_[i].fd_] = i;
    ref_[TimeHeap_[j].fd_] = j;
}

//heap  1  2        2  1  (fd)
//      0  1        0  1  (index)

//map   [1,0] [2,1]     [2,0]  [1,1]


/*
* @brief:
*     清空堆管理内容
*/
void HeapTimer::clear()
{
    TimeHeap_.clear();
    ref_.clear();
}

/*
* @brief:
*     指定结点向上调整
*/
void HeapTimer::siftup(size_t i)
{
    assert(i < TimeHeap_.size());
    size_t parent = (i - 1) / 2;

    while(i > 0)
    {
        //父大于子，交换父子，父子继续迭代，否则退出
        if(TimeHeap_[parent] > TimeHeap_[i])//子小于父
        {
            swapnode(parent,i);//交换
            i = parent;
            parent = (parent - 1) / 2;
        }
        else
        {
            break;
        }
    }
}

/*
* @brief:
*     指定结点向下调整,并返回是否调整
* @parameter:
      n：结点总个数
*/
bool HeapTimer::siftdown(size_t i,size_t n)
{
    assert(i < TimeHeap_.size());
    assert(n <= TimeHeap_.size());
    //已知父结点，判断左右孩子那个更小，与更小的比较，父大于子则交换，否则跳出
    size_t child = (i - 1) / 2;
    size_t index = i;

    while(child < n)
    {
        if(child + 1 < n && TimeHeap_[child] > TimeHeap_[child + 1])
        {
            child += 1;//右小选右
        }
        if(TimeHeap_[i] > TimeHeap_[child])
        {
            swapnode(i,child);
            i = child;
            child = child * 2 + 1;
        }
        else
        {
            break;
        }
    }
    
    return i > index; 
}

/*
* @brief:
*     删除指定的结点
* @parameter:
      t：指定要删除的结点
*/
void HeapTimer::del_(size_t t)
{
    //将要删除的结点换到队尾，然后调整堆，如果就是队尾则无需调整，否则换到队尾后调整，再弹出heap并擦除u_map
    assert(t < TimeHeap_.size());

    size_t n = TimeHeap_.size() - 1;  //尾结点下标
    size_t tmp = t;

    if(tmp < n)
    {
        swapnode(tmp,n);
        if(!siftdown(tmp,n))
        {
            siftup(tmp);
        }
    } 

    //弹出尾结点并擦去u_map
    ref_.erase(TimeHeap_.back().fd_);
    TimeHeap_.pop_back();
}

/*
* @brief:
*     调整指定fd的定时器的expire_属性
* @parameter:
      fd：指定文件描述符
      newExpiresMs:新的过期时间
*/
void HeapTimer::adjust(int fd, int newExpiresMs)  
{
    //时间变了，在堆的下标就改变，下标改变对应u_map就改变(在上下调整就默认执行)，在堆中重新调整
    assert(!TimeHeap_.empty() && ref_.count(fd) > 0);

    size_t index = ref_[fd];
    TimeHeap_[index].expire_ = Clock::now() + Ms(newExpiresMs);

    if(!siftdown(index,TimeHeap_.size()))
    {
        siftup(index);        
    }
}

/*
* @brief:
*     添加新的时间点
* @parameter:
      参数不使用TimeNode是为了避免调用方还要进行繁琐的时间运算
*/
void HeapTimer::add(int fd,int TimeoutMs,TimeoutCallback tcb)
{
    //如果是旧fd，相当于更新后重置回调函数
    //如果是新fd，则插入heap后,更新ref，向上调整
    assert(fd >= 0);
    
    if(ref_.count(fd) > 0)
    {
        size_t index = ref_[fd];
        TimeHeap_[index].tcb = std::move(tcb);        
        adjust(fd,TimeoutMs);
    }
    else
    {
        size_t last = TimeHeap_.size();
        ref_[fd] = last;
        TimeHeap_.push_back({fd,Clock::now() + Ms(TimeoutMs),tcb});
        siftup(last);
    }
} 

/*
* @brief:
*     弹出堆顶
*/
void HeapTimer::top_pop()                                   
{
    assert(!TimeHeap_.empty());
    this->del_(0);
}

/*
* @brief:
*     主动触发并删除定时器 (例如客户端主动断开)
*/
void HeapTimer::doWork(int fd)                        
{
    if(TimeHeap_.empty() || ref_.count(fd) == 0 )//堆为空或者搜索表没有该结点
    {
        return;
    }

    size_t index = ref_[fd];
    TimeHeap_[index].tcb();    //执行回调
    this->del_(index);         //删除堆上对应的结点并擦去查询表
}

/*
* @brief:
*     检查并清理所有已超时的定时器 (EventLoop 的心跳函数)
*/
void HeapTimer::tick()                                     
{
    //检查堆顶时间在高精度时钟下是否小于0，小于则执行回调并弹出该结点
    if(TimeHeap_.empty())
    {
        return;
    }

    while(!TimeHeap_.empty())
    {
        auto Timenode = TimeHeap_.front();
        if(std::chrono::duration_cast<Ms>(Timenode.expire_ - Clock::now()).count() > 0)
        {
            break;
        }        
        Timenode.tcb();
        top_pop();
    }

}

/*
* @brief:
*     获取距离下一个定时器超时的剩余时间 (用于 epoll_wait)
*/
int HeapTimer::Getclosetick()         
{                 
    //先清空旧定时器，记录顶端计时器时间并返回，如果-1代表计时器为空，为0代表超时却未唤醒
    this->tick();
    int res = -1;

    if(!TimeHeap_.empty())
    {
        res = std::chrono::duration_cast<Ms>(TimeHeap_.front().expire_ - Clock::now()).count();
        if(res < 0)
        {
            res = 0;
        }
    }

    return res;
}