#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LEFT "["

#define RIGHT "]"

#define LINE_SIZE 1024  // 命令行缓冲区大小

#define ARGV_SIZE 32

#define DELIM " \t"

#define EXIT_CODE 33

// 以下的几个宏表示重定向的属性

#define IN_RDIR 0  // 输入重定向

#define OUT_RDIR 1  // 输出重定向

#define APPEND_RDIR 2  // 追加重定向

#define NONE -1  // 无属性 即非重定向

char* rdirfilename = NULL;  // 文件名

int rdir = NONE;  // rdir默认为-1 表示非重定向

int lastcode = 0;

int quit = 0;

char commandline[LINE_SIZE];

char* argv[ARGV_SIZE];

char pwd[LINE_SIZE];

char currvariables[LINE_SIZE];

const char* getUsername() { return getenv("USER"); }

const char* getHostname() { return getenv("HOSTNAME"); }

extern int buildCommand(int _argc, char** _argv);

void getpwd() {
  // return getenv("PWD");
  getcwd(pwd, sizeof(pwd));
}

void check_redir(char* cmd) {
  // 检查字符串是否存在重定向
  char* pos = cmd;
  rdir = NONE;
  rdirfilename = NULL;
  while (*pos) {
    if (*pos == '>') {
      // 输出重定向'>' 或是追加重定向 '>>'
      if (*(pos + 1) == '>') {
        /* 追加重定向 */
        // 追加重定向>>
        *pos++ = '\0';
        *pos++ = '\0';
        while (isspace(*pos)) {
          pos++;
        }
        rdirfilename = pos;
        // 将全局变量中的文件名设置为pos字符串
        rdir = APPEND_RDIR;
        // 追加重定向
        break;
      }
      *pos++ = '\0';
      while (isspace(*pos)) {
        pos++;
      }
      rdirfilename = pos;
      // 将全局变量中的文件名设置为pos字符串
      rdir = OUT_RDIR;
      // 输出重定向

      break;
    } else if (*pos == '<') {
      // 输出重定向
      *pos++ = '\0';
      while (isspace(*pos)) {
        pos++;
      }
      rdirfilename = pos;
      // 将全局变量中的文件名设置为pos字符串
      rdir = IN_RDIR;
      // 输入重定向
      break;
    } else {
      // 非重定向
      //  do nothing
    }
    pos++;
  }
}

void interact(char* cline, size_t size) {
  // 获取环境变量为标识符
  /*
      [ USER@HOSTNAME PWD ]label
  */
  getpwd();
  printf(LEFT "%s@%s %s" RIGHT "# ", getUsername(), getHostname(), pwd);

  char* s = fgets(cline, size, stdin);  // 从键盘获取
  if (!s) {
    quit = 1;
    printf("\n");
    return;
  }

  size_t len = strlen(cline);

  if (len > 1) cline[len - 1] = '\0';

  check_redir(cline);

  return;
}

int splitstring(char* cline, char** argv) {
  int i = 0;
  argv[i++] = strtok(cline, DELIM);
  while ((argv[i++] = strtok(NULL, DELIM))) {
    ;
  }
  return i - 1;
}

void normalExecute(char** _argv) {
  //
  pid_t id = fork();
  if (id < 0) {
    //
    perror("fork error");
    return;
  } else if (id == 0) {
    // 子进程执行程序
    int fd = -1;
    int dupret = 0;
    // 判断是否存在重定向 即rdir是否具有参数 (非NONE)
    if (rdir == IN_RDIR) {
      fd = open(rdirfilename, O_RDONLY);
      if (fd < 0) {
        perror("open fail");
        _exit(EXIT_CODE);
      }
      dupret = dup2(fd, 0);
    } else if (rdir == OUT_RDIR) {
      fd = open(rdirfilename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd < 0) {
        perror("open fail");
        _exit(EXIT_CODE);
      }

      dupret = dup2(fd, 1);
    } else if (rdir == APPEND_RDIR) {
      fd = open(rdirfilename, O_WRONLY | O_CREAT | O_APPEND, 0666);
      if (fd < 0) {
        perror("open fail");
        _exit(EXIT_CODE);
      }
      dupret = dup2(fd, 1);
    } else {
      // rdir == NONE
    }

    if (rdir != NONE && dupret < 0) {
      perror("dup fail");
      _exit(EXIT_CODE);
    }

    if (fd >= 0) {
      close(fd);
    }

    execvp(_argv[0], _argv);
    perror("execvp fail");
    _exit(EXIT_CODE);  // 进程替换失败
  } else {
    // 父进程等待子进程
    int status = 0;
    int ret = waitpid(id, &status, 0);  // 等待成功返回pid 失败则返回-1
    if (ret == id) {
      lastcode = WEXITSTATUS(status);
    }
  }
}

int buildCommand(int _argc, char** _argv) {
  getpwd();
  if (_argc == 2 && (strcmp(_argv[0], "cd") == 0)) {
    int dirret = chdir(_argv[1]);
    if (dirret != 0) {  // 说明chdir调用失败
      perror("chdir error\n");
    } else {
      sprintf(getenv("PWD"), "%s", pwd);
    }
    return 1;
  }

  else if (_argc == 2 && strcmp(_argv[0], "export") == 0) {
    // 两种方法都适用
    // ①
    /* char* _name = strtok(_argv[1],"=");
    char* _value = strtok(NULL, "");
    if(_name&&_value){
      setenv(_name, _value, 1);

      //使用putenv的话只是单纯的将字符串的指针写在了环境变量表之中
      //而argv当中的数据是不停变化的
      // 故若是使用putenv的话由于argv的内容在不停变化
      //则会使这个环境变量表的指针指向一个无效的位置
      //故要么将这个环境变量表的指针单独存放一个位置 要么就使用setenv
    }*/

    // ②
    strcpy(currvariables, _argv[1]);
    putenv(currvariables);
    return 1;
  }

  else if (_argc == 2 && strcmp(_argv[0], "echo") == 0) {
    if (strcmp(_argv[1], "$?") == 0) {
      printf("%d\n", lastcode);
      lastcode = 0;
    } else if (_argv[1][0] == '$') {
      // 说明需要打印的是环境变量
      char* val = getenv(_argv[1] + 1);
      if (val) printf("%s\n", val);
    } else {
      // 说明不属于内建命令可以直接打印
      printf("%s\n", _argv[1]);
    }
    return 1;
  }

  else if (strcmp(_argv[0], "ls") == 0) {
    _argv[_argc++] = "--color";
    _argv[_argc] = NULL;
  }
  return 0;
}

int main() {
  rdir = NONE;
  rdirfilename = NULL;
  while (!quit) {
    // 获取命令行信息
    interact(commandline, sizeof(commandline));
    if (quit) break;

    // debug 打印测试输入信息是否正确
    printf("echo: %s\n", commandline);

    // 分割命令行信息
    int argc = splitstring(commandline, argv);  // 分割字符串
    if (argc == 0) continue;
    /*
        如果未进行判断的话 将会发生下列问题:
        当只输入分隔符的情况下数据将不被分割
        对应的argv当中则只有NULL
          由于没有中断此次循环将会继续向下执行
          而在接下来的执行当中 程序将期望存在一个有效的命令
          然而argv[0]为NULL 将会进行报错
    */

    //  //用于debug 对指针数组进行打印
    // for (int i = 0; argv[i] != NULL; ++i) {
    //   printf("%s\n", argv[i]);
    // }

    // 内建命令
    int flag = buildCommand(argc, argv);
    // 重定向暂不考虑内建命令
    // 重定向只修改对应的通常命令

    // // 分割命令后将对命令进行处理
    if (!flag) normalExecute(argv);
    // 重定向将在该接口内进行处理
  }
  return 0;
}

