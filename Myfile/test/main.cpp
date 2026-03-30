#include "Myfile.hpp"

// 测试代码概念
void test_performance() {
    // 测试1：无缓冲（直接fd）
    int fd = open("test1.txt", O_CREAT | O_WRONLY | O_TRUNC, CREAT_FILE_MODE);
    if (fd == -1) {
        perror("open test1.txt error");
        return;
    }
    clock_t start = clock();
    for (int i = 0; i < 10000; i++) {
        int ret = write(fd, "temp data", strlen("temp data"));  // 10000次系统调用！
        if (ret == -1) {
            perror("write error");
            close(fd);
            return;
        }
    }
    close(fd);
    clock_t time1 = clock() - start;
    
    // 测试2：有缓冲（FILE*）
    _FILE* fp = _fopen("test2.txt", "w");  // 标准库
    if (fp == NULL) {
        perror("_fopen test2.txt error");
        return;
    }
    start = clock();
    const char* str = "temp data";
    for (int i = 0; i < 10000; i++) {
        _fwrite(str, sizeof(char), strlen(str), fp);
    }
    _fclose(fp);  // 最后一次系统调用
    clock_t time2 = clock() - start;
    
    printf("无缓冲: %ld ms\n", time1);
    printf("有缓冲区: %ld ms\n", time2);
    printf("提升倍数: %.2f倍\n", (double)time1/time2);
}

int main()
{
    test_performance();

    return 0;
}