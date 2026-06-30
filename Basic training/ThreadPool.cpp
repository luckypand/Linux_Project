#include <queue>
#include <vector>
#include <thread>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <future>

class ThreadPool
{
public:
    explicit ThreadPool(size_t n)
        :shutdown_(false)
    {
        for(int i = 0;i < n;i++)
        {
            workers_.emplace_back(
                [this](){
                    while(true)
                    {
                        //线程加锁 1.判断是否关闭或有新任务 2.取出新任务
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> locker_(mtx_);
                            con_var_.wait(locker_,[this](){
                                return shutdown_ || !tasks_.empty();
                            });

                            if(shutdown_ && tasks_.empty())
                            {
                                return;
                            }
                            task = tasks_.front();
                            tasks_.pop();                            
                        }
                        //锁外执行任务
                        task();
                    }
                }                
            );
        }
    }

    template<class F,class... Args>
    auto Enqueue(F&&f,Args&&... args)
        ->std::future<std::invoke_result<F,Args...>>
    {
        using return_type = std::invoke_result<F,Args...>;
        // 1. 用 packaged_task 包装任务
        //task = make_shared(packaged_task<返回类型（具体函数）>)
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<F> (f),
            ...params = std::forward<Args>(args)]() mutable {
                return std::invoke(func,params...);
            }
        );
        // 2. 获取 future
        std::future<return_type> res = task->get_future();
        // 3. 加锁将任务入队 lambda（替代 bind）
        {
            std::lock_guard<std::mutex> locker(mtx_);
            if(shutdown_)
            {
                throw std::runtime_error("enqueue in shutdown threadpool");
            }   

            tasks_.emplace([task](){
                (*task)();
            });              
        }

        // 4.唤醒一个消费线程，返回结果
        con_var_.notify_one();
        return res;
    }

    ~ThreadPool()
    {
        //加锁修改关闭标志位
        {
            std::unique_lock<std::mutex> locker_(mtx_);
            shutdown_ = true;
        }
        //唤醒此时所有沉睡的线程
        con_var_.notify_all();
        //挨个进行回收
        for(auto& t : workers_)
        {
            if(t.joinable())
            {
                t.join();
            }
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::condition_variable con_var_;
    std::mutex mtx_;
    bool shutdown_;
};