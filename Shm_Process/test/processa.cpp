#include "comm.hpp"

int main() {
//   PipeInit pi;
  log(DEBUG, "The PID of the processa:%d\n", getpid());

  int shmid = CreatShm();  // 创建共享内存并获取shmid
  // sleep(2);
  char* shmaddr = (char*)shmat(shmid, nullptr, 0);  // 挂接共享内存
  log(DEBUG, "ProcessA attach shared memory done\n");
  // sleep(2);

  // ipc code 通信代码
  int fd = open(PIPEFILE, O_RDONLY);
  if (fd < 0) {
    log(FATAL, "open pipefile error :%s\n", strerror(errno));
    exit(FIFO_OPEN_ERR);
  }
  log(INFO, "open pipefile sucess\n");

  while (true) {
    char c;
    int n = read(fd, &c, sizeof(c));
    if (c == 'c') {
      cout << "processa get massage@ " << shmaddr << endl;
    }
    if(n <= 0)
    {
        break;
    }

    // 查看共享内存信息代码
    // struct shmid_ds shmds;
    // shmctl(shmid, IPC_STAT, &shmds);
    // printf("The shm key :0x%x\n", shmds.shm_perm.__key);
    // cout << "The shm size :" << shmds.shm_segsz << endl;
    // cout << "PID of creator :" << shmds.shm_cpid << endl;
    // cout << "===========================" << endl;

    // sleep(2);
  }

  shmdt(shmaddr);
  log(DEBUG, "ProcessA disattach shared memory done\n");
  // sleep(2);
  int n = shmctl(shmid, IPC_RMID, nullptr);
  if (n < 0) {
    log(FATAL, "ProcessA shmctl faile : %s\n", strerror(errno));
    exit(3);
  }
  log(DEBUG, "ProcessA free the shared memory sucess\n");
  // sleep(2);
  log(DEBUG, "ProcessA quit....\n");

  return 0;
}//
