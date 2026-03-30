#include "comm.hpp"

int main() {
  // cout << "hello world processb" << endl;
  int shmid = GetShm();  // 获取相同key下已经存在的共享内存的shmid
  std::string s = " ";
  char* shmaddr = (char*)shmat(shmid, nullptr, 0);  // 挂接共享内存
  // sleep(5);
  log(DEBUG, "ProcessB attach shared memory done\n");
  // sleep(10);

  // ipc code
  int fd = open(PIPEFILE, O_WRONLY);
  if (fd < 0) {
    log(FATAL, "open pipefile error :%s\n", strerror(errno));
    exit(FIFO_OPEN_ERR);
  }
  log(INFO, "open pipefile sucess\n");

  while (true) {
    cout << "Please enter$ ";
    cin >> s;
    strcpy(shmaddr,s.c_str());
    write(fd, "c", 1);
  }

  shmdt(shmaddr);
  log(DEBUG, "ProcessB disattach shared memory done\n");
  // sleep(100);
  log(DEBUG, "ProcessB quit....\n");
  return 0;
}