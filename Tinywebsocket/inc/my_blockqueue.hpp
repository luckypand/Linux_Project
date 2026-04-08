#pragma once
#include <assert.h>
#include <deque>
#include <iostream>
#include <mutex>
#include <condition_variable>

using namespace std;

template <typename T>
class Blockqueue
{
public:
    //class 
    explicit Blockqueue(size_t max_size = 1024); //显示定义
    ~Blockqueue();

    //resocure access
    T front();
    T back();
    size_t capacity();
    size_t size();
    bool empty();
    bool full();

    //modify
    void push_back(const T& item);
    void push_front(const T& item); 
    bool pop(T& item);  // 弹出的任务放入item
    bool pop(T& item, int timeout);  // 等待时间

    //log operation
    void Clear();
    void Close();
    void flush();
private:
    deque<T> deq_;                       //底层结构
    size_t capacity_;                    //容量
    bool isClose_;                       //是否关闭
    mutex mtx_;                          //互斥锁
    condition_variable condConsumer_;    //消费者条件变量
    condition_variable condProducer_;    //生产者条件变量

};

template<typename T>
Blockqueue<T>::Blockqueue(size_t max_size)
    :capacity_(max_size)
{
    assert(capacity_ > 0);
    isClose_ = false;
}

template<typename T>
void Blockqueue<T>::flush()
{
    condConsumer_.notify_one();         //唤醒消费者
}

template<typename T>
void Blockqueue<T>::Clear()
{
    lock_guard<mutex> locker(mtx_);
    deq_.Clear();
}

template<typename T>
void Blockqueue<T>::Close()
{
    Clear();
    isClose_ = true;
    condConsumer_.notify_all();//唤醒消费者
    condProducer_.notify_all();//唤醒生产者
}   

template<typename T>
Blockqueue<T>::~Blockqueue()
{
    Close();
}

template<typename T>
size_t Blockqueue<T>::capacity()
{
    lock_guard<mutex> locker(mtx_);//上锁
    return capacity_;
}

template<typename T>
size_t Blockqueue<T>::size()
{
    lock_guard<mutex> locker(mtx_);//上锁
    return deq_.size();
}

template<typename T>
bool Blockqueue<T>::empty()
{
    lock_guard<mutex> locker(mtx_);//上锁
    return size() == 0;
}

template<typename T>
bool Blockqueue<T>::full()
{
    lock_guard<mutex> locker(mtx_);//上锁
    return capacity() == size();
}

template<typename T>
void Blockqueue<T>::push_back(const T& item)
{
    //流程：上锁->判断队列是否满->如果满了，生产者等待->如果不满了，加入元素->唤醒消费者
    unique_lock<mutex> locker(mtx_);//上锁
    while(full())
    {
        condProducer_.wait(locker);
    }
    deq_.push_back(item);
    condConsumer_.notify_one();//唤醒消费者
}

template<typename T>
void Blockqueue<T>::push_front(const T& item)
{
    //流程：上锁->判断队列是否满->如果满了，生产者等待->如果不满了，加入元素->唤醒消费者
    unique_lock<mutex> locker(mtx_);//上锁
    while(full())
    {
        condProducer_.wait(locker);
    }
    deq_.push_front(item);
    condConsumer_.notify_one();//唤醒消费者
}
/*
* @brief:
     *pop()默认使用pop_front，因为可以搭配log中的push_back()
*  实现push_back + pop_front => 标准 FIFO，先来先处理或者
*  push_front + pop_front => 新来的任务插到最前面，结果更像 LIFO，后来的先处理
*/
template<typename T>
bool Blockqueue<T>::pop(T& item)
{
    //流程：上锁->判断队列是否空->如果空了，消费者等待->如果不空了，弹出元素->唤醒生产者
    unique_lock<mutex> locker(mtx_);//上锁
    while(empty())
    {
        condConsumer_.wait(locker);
    }
    item = deq_.front();//返回删除值给调用方
    deq_.pop_front();
    condProducer_.notify_one();//唤醒生产者
    return true;
}

/*
* @brief:
*     限时等待，防止线程卡死，所以与常规直接使得消费者等待不同，需要进行超时判断后直接
*  返回，或者在等待途中发现log关闭，也直接退出
*/
template<typename T>
bool Blockqueue<T>::pop(T& item, int timeout)
{
    unique_lock<mutex> locker(mtx_);//上锁
    while(empty())//判空
    {
        if(condConsumer_.wait_for(locker, std::chrono::seconds(timeout)) 
                == std::cv_status::timeout)
            {
                return false;
            }
        if(isClose_)
        {
            return false;
        }
    }
    item = deq_.front();//返回删除值给调用方
    deq_.pop_front();
    condProducer_.notify_one();//唤醒生产者
    return true;    
}

template<typename T>
T Blockqueue<T>::front()
{
    lock_guard<mutex> locker(mtx_);//上锁
    return deq_.front();
}

template<typename T>
T Blockqueue<T>::back()
{
    lock_guard<mutex> locker(mtx_);//上锁
    return deq_.back();
}