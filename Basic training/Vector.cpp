#include <iostream>
#include <assert.h>

template<typename T>
class Vector
{
public:
    Vector()
        :data_(nullptr)
        ,capacity_(0)
        ,size_(0)
    {
    
    }

    Vector(const Vector& other) //拷贝构造
    {
        capacity_ = other.capacity_;
        size_ = other.capacity_;

        data_ = new T(other.capacity_);

        for(int i = 0;i < other.size_;i++)//拷贝other内容
        {
            data_[i] = other.data_[i];
        }
    }

    Vector& operator=(const Vector& other) //拷贝赋值
    {
        size_ = other.size_;
        capacity_ = other.capacity_;
        
        delete[] data_;//删除原数组
        data_ = new T(capacity_);

        for(int i = 0;i < other.size_;i++)//拷贝other内容
        {
            data_[i] = other.data_[i];
        }

        return *this;
    }

    void push_back(const T& value)
    {
        //如果满了，扩容
        if(size_ >= capacity_)
        {
            resize();
        }
        data_[size_++] = value;
    }

    void pop_back()
    {
        if(size_ > 0)
        {
            size_--;
        }
    }

    T& operator[](size_t index)
    {

        assert(index < size_);

        return data_[index];
    }

    ~Vector()
    {
        delete[] data_;
    }

    size_t size()
    {
        return size_;
    }
private:
    void resize()
    {
        capacity_ == 0 ? 4 : capacity_ * 2;
        T* new_data_ = new T(capacity_);

        for(int i = 0;i < size_;i++)
        {
            new_data_[i] = data_[i];
        }
        delete[] data_;
        data_ = new_data_;
    }

    T* data_;
    size_t capacity_;
    size_t size_;
};