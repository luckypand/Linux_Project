#ifndef __MYSTDIO_H__
#define __MYSTDIO_H__
/*
  此处的
    #ifndef
    #define
    #endif
  为"包含卫士" 
  其作用于 #program once 相同
  用于避免头文件重复包含所引起的重复编译问题
*/

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

//定义缓冲区刷新策略
#define NO_BUFFER 1 //无缓冲
#define ROW_BUFFER 2 //行缓冲
#define BLOCK_BUFFER 4  // 块缓冲(全缓冲)

#define CREAT_FILE_MODE 0666
#define BUFFER_SIZE 1024


typedef struct _IO_FILE_{
  int _fileno;
  char _buf[BUFFER_SIZE];
  int _pos;//用于标定文件流的位置 在写入与读取时文件流的位置
  int _flushmode;
} _FILE;

_FILE *_fopen(const char *path, const char *mode);

size_t _fwrite(const void*ptr,size_t size,size_t nmemb,_FILE*fp);

int _fclose(_FILE *fp);

int _fflush(_FILE *fp);

size_t _fread(void *ptr, size_t size, size_t nmemb, _FILE *fp);

#endif 
