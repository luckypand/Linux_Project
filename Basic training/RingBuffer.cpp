#include <vector>
#include <string>

class RingBuffer
{
private:
    int head{0};
    int tail{0};
    int size{0};
    int capacity{0};
    std::vector<char> RingBuffer;

public:
    bool push(char& c) 
    {
        if(full())
        {
            return false;
        }
        RingBuffer[head] = c;
        head = (head + 1) % capacity;
        size++;
        return true;
    }

    bool pop(char& c)
    {
        if(empty())
        {
            return false;
        }
        c = RingBuffer[tail];
        tail = (tail + 1) % capacity;
        size--;
        return true;
    }
private:
    bool full() { return capacity == size; }
    bool empty() { return capacity == 0; }
};