#include <string>

// 共享内存池 — POSIX 共享内存 RAII 封装
class ShmMemoryPool
{
public:
    // 打开模式：CREATE 先 unlink 再创建（服务端），ATTACH 直接打开已有段（客户端）
    enum Mode { CREATE, ATTACH };

    explicit ShmMemoryPool(const std::string& name, size_t size, Mode mode = CREATE);
    ~ShmMemoryPool();
    void* GetMappedptr();
private:
    std::string name_;
    size_t size_;
    void* mapped_ptr_;
    Mode mode_;
};
