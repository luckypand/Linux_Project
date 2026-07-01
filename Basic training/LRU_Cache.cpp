#include <unordered_map>
using namespace std;

class LRU_Cache
{
public:
    LRU_Cache(int capacaty = 4)
        :capacity_(capacaty)
        ,dummy_head(nullptr)
        ,dummy_tail(nullptr)
    {
        dummy_head = new Node();
        dummy_tail = new Node();
        dummy_head->next = dummy_tail;
        dummy_head->prev = nullptr;
        dummy_tail->prev = dummy_head;
        dummy_tail->next = nullptr;
    }
    //插入新Cache
    void put(int key,int value)
    {
        if(cache_.count(key)) //已存在 
        {
            //将其移至队头后更新值
            Node* node_ = cache_[key];
            MoveToHead(node_);
            node_->value_ = value;
            return;
        }
        //不存在,更新cache后判断容量是否超过
        Node* node_ = new Node(key,value);
        cache_[key] = node_; //更新cache
        AddToHead(node_);
        if(cache_.size() > capacity_)
        {
            Node* del = DelTail();
            cache_.erase(del->key_);
            delete del;
        }        
    }

    //读取Cache值
    int get(int key)
    {
        if(!cache_.count(key)) //不存在
        {
            return -1;
        }
        Node* node_ = cache_[key];
        MoveToHead(node_);
        return node_->value_;
    }

private:
    struct Node;
    //头插
    void AddToHead(Node* node)
    {
        node->next = dummy_head->next;
        node->prev = dummy_head;
        dummy_head->next->prev = node;
        dummy_head->next = node; 
    }
    //移除(未删除结点,需要手动释放)
    void RemoveNode(Node* node)
    {   
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }
    //移首
    void MoveToHead(Node* node)
    {
        RemoveNode(node); //先移除再头插
        AddToHead(node);
    }
    //尾删(未删除结点,需要手动释放)
    Node* DelTail()
    {
        Node* node_ = dummy_tail->prev;
        RemoveNode(node_);
        return node_;
    }

    struct Node
    {
        int key_;
        int value_;
        
        Node* prev;
        Node* next;

        Node(int key = 0,int value = 0)
            :key_(key)
            ,value_(value)
            ,prev(nullptr)
            ,next(nullptr)
        {

        }
    };
    unordered_map<int ,Node*> cache_;
    int capacity_;
    Node* dummy_head;
    Node* dummy_tail;
};  