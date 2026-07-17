#include <cstring>
#include <cstddef>

static inline size_t strlcpy_compat(char* dst, const char* src, size_t dstsize)
{
    size_t srclen = std::strlen(src);
    if (dstsize != 0) {
        size_t copylen = (srclen >= dstsize) ? dstsize - 1 : srclen;
        std::memcpy(dst, src, copylen);
        dst[copylen] = '\0';
    }
    return srclen;
}

class String
{
public:
    String()
        :str(nullptr)
        ,len(0)
    {
        str = new char[1];
        str[0] = '\0';
    }

    String(const char* s)
    {
        len = strlen(s);
        str = new char[len + 1];
        strlcpy_compat(str, s, len + 1);
    }

    String(const String& other)
    {
        len = other.len;
        str = new char[len + 1];
        strlcpy_compat(str, other.str, len + 1);
    }

    String& operator=(const String& other)
    {
        if(&other == this)
        {
            return *this;
        }
        delete[] str;

        len = other.len;
        str = new char[len + 1];
        strlcpy_compat(str, other.str, len + 1);

        return *this;
    }

    ~String()
    {
        delete[] str;
    }
private:
    const char* c_str() const
    {
        return str;
    }

    size_t size() const
    {
        return len;
    }

    char* str;
    size_t len;
};