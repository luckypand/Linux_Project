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
    explicit Blockqueue(size_t max_size = 1024);
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
    bool pop(T& item);
    bool pop(T& item, int timeout);

    //log operation
    void Clear();
    void Close();
    void flush();
private:
    deque<T> deq_;
    size_t capacity_;
    bool isClose_;
    mutex mtx_;
    condition_variable condConsumer_;
    condition_variable condProducer_;

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
    condConsumer_.notify_one();
}

template<typename T>
void Blockqueue<T>::Clear()
{
    lock_guard<mutex> locker(mtx_);
    deq_.clear();
}

template<typename T>
void Blockqueue<T>::Close()
{
    Clear();
    isClose_ = true;
    condConsumer_.notify_all();
    condProducer_.notify_all();
}

template<typename T>
Blockqueue<T>::~Blockqueue()
{
    Close();
}

template<typename T>
size_t Blockqueue<T>::capacity()
{
    lock_guard<mutex> locker(mtx_);
    return capacity_;
}

template<typename T>
size_t Blockqueue<T>::size()
{
    lock_guard<mutex> locker(mtx_);
    return deq_.size();
}

template<typename T>
bool Blockqueue<T>::empty()
{
    lock_guard<mutex> locker(mtx_);
    return deq_.size() == 0;  // direct access to avoid recursive lock from size()
}

template<typename T>
bool Blockqueue<T>::full()
{
    lock_guard<mutex> locker(mtx_);
    return capacity_ == deq_.size();  // direct access to avoid recursive lock
}

template<typename T>
void Blockqueue<T>::push_back(const T& item)
{
    unique_lock<mutex> locker(mtx_);
    while(deq_.size() >= capacity_ && !isClose_)  // 队列满且未关闭时才等待
    {
        condProducer_.wait(locker);
    }
    if(isClose_) return;  // 队列已关闭，丢弃数据（避免 shutdown 时生产者死等）
    deq_.push_back(item);
    condConsumer_.notify_one();
}

template<typename T>
void Blockqueue<T>::push_front(const T& item)
{
    unique_lock<mutex> locker(mtx_);
    while(deq_.size() >= capacity_ && !isClose_)  // 队列满且未关闭时才等待
    {
        condProducer_.wait(locker);
    }
    if(isClose_) return;  // 队列已关闭，丢弃数据
    deq_.push_front(item);
    condConsumer_.notify_one();
}

/*
 * pop() uses pop_front to pair with push_back, implementing standard FIFO.
 * Returns false if queue is closed while waiting.
 */
template<typename T>
bool Blockqueue<T>::pop(T& item)
{
    unique_lock<mutex> locker(mtx_);
    while(deq_.empty())  // 直接判断，避免 empty() 递归加锁导致死锁
    {
        if(isClose_)
        {
            return false;  // queue closed, exit to avoid deadlock on shutdown
        }
        condConsumer_.wait(locker);
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

/*
 * pop with timeout. Also checks isClose_ to allow graceful shutdown.
 */
template<typename T>
bool Blockqueue<T>::pop(T& item, int timeout)
{
    unique_lock<mutex> locker(mtx_);
    while(deq_.empty())  // 直接判断，避免 empty() 递归加锁导致死锁
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
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

template<typename T>
T Blockqueue<T>::front()
{
    lock_guard<mutex> locker(mtx_);
    return deq_.front();
}

template<typename T>
T Blockqueue<T>::back()
{
    lock_guard<mutex> locker(mtx_);
    return deq_.back();
}