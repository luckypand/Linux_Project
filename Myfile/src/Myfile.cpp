#include "Myfile.hpp"

/* "w" "r" "a" 三种打开方式*/
_FILE *_fopen(const char *path, const char *mode) {
  int fd = 0;
  if (strcmp(mode, "w") == 0) {
    fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, CREAT_FILE_MODE);
  } else if (strcmp(mode, "r") == 0) {
    fd = open(path, O_RDONLY);
  } else if (strcmp(mode, "a") == 0) {
    fd = open(path, O_CREAT | O_WRONLY | O_APPEND, CREAT_FILE_MODE);
  } else {
    return NULL;
    // 其他选项暂不考虑实现
  }
  if (fd == -1) return NULL;
  _FILE *fp = (_FILE *)malloc(sizeof(_FILE));
  fp->_fileno = fd;
  fp->_pos = 0;                // 默认为0;
  // fp->_flushmode = ROW_BUFFER;  // 默认刷新策略为行缓冲
  fp->_flushmode = BLOCK_BUFFER;  // 默认刷新策略修改为全缓冲
  return fp;
}

size_t _fwrite(const void *ptr, size_t size, size_t nmemb, _FILE *fp) {
  int len = nmemb * size;
  memcpy(&fp->_buf[fp->_pos], ptr, len);
  fp->_pos += len;  // 更新文件流所指位置
  int ret = 0;
  if (fp->_flushmode & NO_BUFFER) {
    // 无缓冲 立即刷新
    ret = write(fp->_fileno, fp->_buf, len);
    fp->_pos = 0;  // 重置位置
  } else if (fp->_flushmode & ROW_BUFFER) {
    // 行缓冲 当遇到\n时进行刷新
   
    /*
      在该接口当中的行缓冲刷新策略并不是做的很好
      只能刷新一次\n
      但本次模拟为简易模拟并不考虑
    */

    int flush_len = 0;
    for (; flush_len < fp->_pos; ++flush_len) {
      if (fp->_buf[flush_len] == '\n') break;
    }
    flush_len += 1;
    ret = write(fp->_fileno, fp->_buf, flush_len);
    fp->_pos = 0;  // 重置位置
  } else {
    // 全缓冲(块缓冲) 当缓冲区满时进行刷新
    if (fp->_pos >= BUFFER_SIZE) {
      ret = write(fp->_fileno, fp->_buf, fp->_pos);
      fp->_pos = 0;  // 重置位置
    }
  }
  if (ret == -1) {
    perror("_fwrite\n");
    return 0;
  }
  return len;
}

size_t _fread(void *ptr, size_t size, size_t nmemb, _FILE *fp) {
  // ptr为用户自行提供的缓冲区
  size_t len = size * nmemb;
  if (len > BUFFER_SIZE) len = BUFFER_SIZE;
  ssize_t pos = read(fp->_fileno, fp->_buf, len);
  // pos为实际读取的数据
  if (pos == -1) {
    perror("_fread -- read\n");
    return 0;
  }
  memcpy(ptr, &fp->_buf[fp->_pos], pos);
  fp->_pos += pos;
  return pos;
}

int _fclose(_FILE *fp) {
  // 关闭文件时需要将对应文件流的缓冲区进行刷新

  _fflush(fp);
  int ret = close(fp->_fileno);
  free(fp);
  if (ret == -1) {
    // 失败
    perror("_fclose\n");
    return -1;
  }
  return 1;
}

int _fflush(_FILE *fp) {
  ssize_t ret = 0;
  if (fp->_pos > 0) {
    ret = write(fp->_fileno, fp->_buf, fp->_pos);
    fp->_pos = 0;//更新文件流的位置
  }
  //判断调用write是否失败
  if(ret == -1){
    return EOF;
  }
  return 0;
}

