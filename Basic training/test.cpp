#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>

class ThreadPool
{
public:
    explicit ThreadPool();
    ~ThreadPool();
private:
    std::vector<std::thread> ThreadPool_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;

    std::condition_variable con_var_;
};