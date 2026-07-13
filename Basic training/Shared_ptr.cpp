#include <iostream>

template<typename T>
class Shared_ptr
{
public:
    Shared_ptr(T* p = nullptr)
        :ptr(p)
    {
        if(ptr) //已经有管理对象(一定是第一个管理对象，不支持使用一个裸指针初始化两次)
        {
            count = new int(1);
        }
        else //该指针还是没有管理对象
        {
            count = nullptr; 
        }
    }

    Shared_ptr(const Shared_ptr& other) //拷贝构造
    {   
        ptr = other.ptr;
        count = other.count;

        if(nullptr == count)
        {
            (*count)++;
        }
    }

    T& operator=(const Shared_ptr& other)//Shared_ptr的拷贝赋值不能保留两份
    {
        if(other == this) //先判断是否为自己
        {
            return *this;
        }

        (*count)--;

        ptr = other.ptr;
        count = other.count;
        if(count)
        {
            (*count)++;
        }
        return *this;
    }

    ~Shared_ptr() { release(); }

    int use_Count()
    {
        if(count)
        {
            return *count;
        }
        return 0;
    }

    T& operator*()
    {
        return *ptr;
    }

    T* operator->()
    {
        return ptr;
    }
private:
    void release()
    {
        if(!count) 
        {
            return;
        }

        (*count)--; 

        if((*count) == 0) //计数为0释放管理对象
        {   
            delete ptr;
            delete count;
        }
    }

private:
    T* ptr;
    int* count;
};