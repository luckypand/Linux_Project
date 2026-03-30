#ifndef __COMM_HPP_
#define __COMM_HPP_

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>

#include <cstring>
#include <iostream>

#include "log.hpp"
using namespace std;

#define PATHNAME "/home/"
#define SIZE 4096
const int proj_id = 0x7878;
Log log;

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

#define PIPEFILE "./myfifo"
#define MODE 0666

#define SIZE 1024

using namespace std;

enum { FIFO_CREATE_ERR = 1, FIFO_DELETE_ERR, FIFO_OPEN_ERR };

class PipeInit {
 public:
  PipeInit() {
    int n = mkfifo(PIPEFILE, MODE);
    if (n == -1) {
      cerr << "CREATE PIPE FAIL" << endl;
      exit(FIFO_CREATE_ERR);
    }
  }
  ~PipeInit() {
    int m = unlink(PIPEFILE);
    if (m == -1) {
      cerr << "DEL PIPE FAIL" << endl;
      exit(FIFO_DELETE_ERR);
    }
  }
};

key_t GetKey() {
  key_t key = ftok(PATHNAME, proj_id);
  if (key < 0) {
    log(FATAL, "ftok error : %s\n", strerror(errno));
    exit(-1);
  }
  log(INFO, "ftok sucess , key@ 0x%x\n", key);
  return key;
}

int GetShareMem(int flag) {
  key_t k = GetKey();
  int shmid = shmget(k, SIZE, flag);
  if (shmid < 0) {
    log(FATAL, "Creat shared memory fail : %s\n", strerror(errno));
    exit(2);
  }
  log(INFO, "Creat shared memory sucess , shmid@ %d\n", shmid);
  return shmid;
}

int CreatShm() { return GetShareMem(IPC_CREAT | IPC_EXCL | 0666); }

int GetShm() { return GetShareMem(IPC_CREAT); }

#endif// it's a test
