#include "RpcCodec.hpp"

void RpcCodec::Onmessage(const TcpConnectionPtr& conn,Buffer& buf)
{
    while(buf.ReadBytes() >= HEADER_SIZE)
    {
        uint32_t magic = buf.peekInt32();
        if(magic != RPC_MAGIC_NUMBER)//魔数不匹配
        {
            conn->shutdown();
            return;
        }
        // 2. 看后 4 字节：获取 Payload 长度
        uint32_t length = buf.peekInt32(HEADER_SIZE - sizeof(decltype(magic)));
        // 3. 防护：防止超大包恶意攻击 (例如限制最大单包 64MB)
        if(length > MAX_SINGLEDATA)
        {
            conn->shutdown();
            return;
        }
        //4.开始判断半包还是全包
        if(buf.ReadBytes() >= HEADER_SIZE + length)
        {
            //至少有个全包
            buf.retrieve(HEADER_SIZE);
            std::string payload = buf.retrieveAsString(length);
            //判断了包后RPC路由执行对应命令
            businessCallback_(conn,payload);
        }
        else
        {
            //半包,退出等待有全包的情况
            break;
        }
    }
}
