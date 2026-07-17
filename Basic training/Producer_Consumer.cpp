#include <mutex>
#include <queue>
#include <condition_variable>

class condition
{
public:
    void Push(char c)
    {
        std::unique_lock<std::mutex> locker(mtx_);  

        while(q_.size() == capacity) //满了
        {
            Notfull.wait(locker);
        }
        
        q_.push(c);
        Notempty.notify_one();
    }

    char Pop()
    {
        std::unique_lock<std::mutex> locker(mtx_);  

        while(q_.empty()) //已经空
        {
            Notempty.wait(locker);
        }   
        
        q_.pop();
        Notfull.notify_one();
    }

private:
    std::queue<char> q_;
    std::mutex mtx_;
    std::condition_variable Notfull;     //代表没装满
    std::condition_variable Notempty;    //代表有数据
    size_t capacity;
};
