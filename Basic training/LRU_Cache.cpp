#include <unordered_map>

class LRU_Cache
{
public:

private:
    struct Node
    {
        int key;
        int value;
        
        Node* prev;
        Node* next;
    };
};