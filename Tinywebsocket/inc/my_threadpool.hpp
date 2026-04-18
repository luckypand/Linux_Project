#pragma once
#include <thread>
#include <mutex>
#include <queue>
#include <functional>
#include <condition_variable>
#include <assert.h>
#include <memory>
#include <utility>
#include <vector>

//使用非detach模式跑通线程池
class ThreadPool
{
public: 
    using WorkerExitReporter = std::function<void(std::thread::id)>;

    explicit ThreadPool(int Thread_count = 8, WorkerExitReporter on_worker_exit = {});           //自定义初始化构造
    ThreadPool(ThreadPool&& ) = default;                 //默认生成移动构造
    ThreadPool(const ThreadPool& ) = delete;             //禁用拷贝构造
    ThreadPool& operator=(const ThreadPool& ) = delete;  //禁用拷贝赋值
    ~ThreadPool();                                       //析构
    template<typename T>
    void Add_Task(T&& task);                             //添加任务
private:
    struct Pool
    {
        std::vector<std::thread> worker_thread_;    //取代detach模式下的join列表
        std::mutex mtx_;                            //线程池锁
        std::queue<std::function<void()>> tasks_;    //任务队列
        std::condition_variable conditon_;          //条件变量
        bool IsClose_ = false;                      //判断线程池是否关闭  
    };
    std::shared_ptr<Pool> pool_ptr_;                //RAII管理线程池资源
    WorkerExitReporter on_worker_exit_;
};

ThreadPool::ThreadPool(int Thread_count, WorkerExitReporter on_worker_exit) //提供一个退出时的回调入口，使得线程退出时能够执行业务代码
    :pool_ptr_(std::make_shared<Pool>())            //make_shared创建智能指针进行管理
    ,on_worker_exit_(std::move(on_worker_exit))
{
    assert(Thread_count > 0);
    //拷贝一份shared_ptr，防止出现先析构后线程还在访问的现象(针对detach模式)
    // auto pool_ptr_copy_ = pool_ptr_;

    //创建线程池(非detach模式)
    for(int i = 0;i < Thread_count;i++)
    {
        pool_ptr_->worker_thread_.emplace_back([this](){ //线程入列
            std::unique_lock<std::mutex> locker(pool_ptr_->mtx_); //加锁
            while(true)
            {
                if(!pool_ptr_->tasks_.empty())//任务队列非空
                {
                    auto task = pool_ptr_->tasks_.front();
                    pool_ptr_->tasks_.pop();
                    locker.unlock();//拿到任务就可以解锁了
                    task();         //线程执行对应任务
                    locker.lock();  //重新竞争锁
                }
                else if(pool_ptr_->IsClose_) //线程池已经关闭
                {
                    if(on_worker_exit_)//回调函数通知当线程死亡时需要执行的任务
                    {
                        //**拥有回调机制时，需要先解锁再让线程执行退出任务**
                        //1.若执行的任务本身需要去去锁，不先释放则死锁
                        //(排队到柜台前，让柜台等自己回来，但是自己又去排队了)
                        //2.减少线程等待时间
                        //(当自己拿到锁但是发现没有任务需要处理，线程就可以自己
                        //去处理自己的回调任务了，没有必要占据锁执行回调任务)
                        locker.unlock();
                
                        on_worker_exit_(std::this_thread::get_id());
                    }
                    break;
                }
                else//任务队列为空，释放锁后线程沉睡
                {
                    pool_ptr_->conditon_.wait(locker);
                }
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    if(pool_ptr_)
    {
        {
            std::unique_lock<std::mutex> locker(pool_ptr_->mtx_);
            pool_ptr_->IsClose_ = true;
        }
        pool_ptr_->conditon_.notify_all();

        //等待线程回收
        for(auto& thread : pool_ptr_->worker_thread_)
        {
            if(thread.joinable())
            {
                thread.join();                
            }
        }
    }
}

/*
* @brief:
*     新增任务队列的任务
*/
template<typename T>
void ThreadPool::Add_Task(T&& task)//该种写法传入左值和右值都有可能,故不能直接使用move
{
    //添加任务是访问共享资源区，需要加锁
    std::unique_lock<std::mutex> locker(pool_ptr_->mtx_);
    //经过函数内部传递时task变为左值，需要完美转发使得其还原为原本的类型T,并
    //利用emplace进行容器的非拷贝传递
    pool_ptr_->tasks_.emplace(std::forward<T>(task));
    //条件变量刚好是判断任务队列是否为空，因此这里需要唤醒卡在wait处的函数
    pool_ptr_->conditon_.notify_one();
}
