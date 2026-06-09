#include <string>

//申请共享内存进行映射
class ShmMemoryPool
{
public:
    explicit ShmMemoryPool(const std::string& name,size_t size);
    ~ShmMemoryPool();
    void* GetMappedptr();
private:
    std::string name_;
    size_t size_;
    void* mapped_ptr_;
};
