#include "Task.hpp"

#include <unistd.h>

#include <cassert>

#include <string>

#include <vector>

#include <cstdlib>

#include <ctime>

#include <sys/wait.h>

#include <sys/stat.h>


#define PROCESSNUM 5  // 控制子进程创建个数

std::vector<task_t> tasks;
int debugn = 0;


/*
    父进程写
    子进程读
*/

/* 先描述 后组织 */

// 描述
class channel {
 public:
  // 构造函数
  channel(int cmdfd, pid_t slaveryid, const std::string& processname)
      : _cmdfd(cmdfd), _slaveryid(slaveryid), _processname(processname) {}

 public:
  int _cmdfd;                // 发送任务用文件描述符
  pid_t _slaveryid;          // 进程的pid
  std::string _processname;  // 进程名
};

void slaver() {
  // 子进程读取父进程
  while (true) {
    /*
        debug
        //每个子进程都将从文件描述符rfd中进行读取
        在当前程序中 rfd为3
    // std::cout << getpid() << " - read fd is : " << rfd << std::endl;
    */

    int cmdcode = 0;
    int n = read(0,&cmdcode,sizeof(int));
    if(n == sizeof(int)){
        //执行cmdcode对应的任务列表
        std::cout <<"slaver get a cmdcode @["<< getpid() << "] : cmdcode : " << cmdcode << std :: endl;
        if (cmdcode < 0 || cmdcode > (int)tasks.size()) continue;
        tasks[cmdcode]();
        std::cout << "--------------------------" << std::endl;
    }
    if(n == 0)//说明写端被关闭 0表示文件结尾
        break;
    // sleep(100);
  }
}


void InitProcessPool(std::vector<channel>* channels) {  // 1.初始化

    //version 2 -- 确保每个子进程只有一个写端
    std::vector<int> oldfds;
    for (int i = 0; i < PROCESSNUM; ++i) {
        // 子进程创建时先创建管道
        int pipefd[2];
        int n = pipe(pipefd);
        assert(!n);  // 差错处理 pipe创建管道失败
        (void)n;

        pid_t id = fork();
        if (id < 0) {
            // 进程创建失败
            std::cerr << "fork errno" << std::endl;
            assert(id >= 0);  // 差错处理 子进程创建失败
        }
        if (id == 0) {
            // 子进程
            close(pipefd[1]);//子进程关闭写端
            for(const auto& fd:oldfds){
                close(fd);
                /*
                    这里的close不会调用失败
                    在下文中的oldfds数组将会记录父进程对上一个子进程的写端 
                    所以会导致子进程会对其他子进程存在写端
                    所以这里的close并不会调用失败 
                    因为对于每个子进程而言 数组中的所有文件描述符都是有效的文件描述符
                */
            }
            // 子进程读 关闭[1]
            /*
                slaver(pipefd[0]);
                这是一种做法 为从这个描述符当中读取任务数据并执行任务
            */
            dup2(pipefd[0], 0);
            // 该种做法为 使用dup2接口进行重定向
            // 使得子进程的默认输入从键盘改为pipefd[0]中读取
            slaver();//默认从文件描述符0 获取任务信息即可
            std::cout << "Process : " << getpid() << " quit sucess" << std::endl;
            exit(0);
    }
    // 父进程
    close(pipefd[0]);  // 父进程关闭读端

    // 添加字段
    std::string name = "Process" + std::to_string(i);

    channels->push_back(channel(pipefd[1], id, name));  // 进行初初始化

    oldfds.push_back(pipefd[1]);//记录写端

    // Debug
    // std::cout << "==============================" << std::endl;
    // std::cout << "对oldfds数组进行打印 [" << debugn <<"]@ "<< std::endl;
    // debugn++;
    // for (const auto& fd : oldfds) {
    //   std::cout << fd << std::endl;
    // }

    // std::cout << "==============================" << std::endl;
    }
}

/*
    编码规范
    输入型参数: const &
    输出型参数: *
    输入/输出型参数: &
*/

void Debug(const std::vector<channel>& channels) {
    for (auto& c : channels) {
    std::cout << "cmdfd : " << c._cmdfd << std::endl
              << "slaveryid : " << c._slaveryid << std::endl
              << "processname : " << c._processname << std::endl;

    std::cout << "---------------------" << std::endl;
  }
}

void Menu(){
    std::cout << "#############################################\n";
    std::cout << "#                 主菜单                    #\n";
    std::cout << "#############################################\n";
    std::cout << "# 1. 数据初始化                             #\n";
    std::cout << "# 2. 更新数据                               #\n";
    std::cout << "# 3. 获取数据                               #\n";
    std::cout << "# 4. 数据验证                               #\n";
    std::cout << "# 5. 生成报告                               #\n";
    std::cout << "# 6. 备份数据                               #\n";
    std::cout << "# 0. 退出                                   #\n";
    std::cout << "#############################################\n";
}


void ControlSlaver(const std::vector<channel> &channels){
//向子进程派发任务
    //需要随机
    Menu();
    for (int i = 1; i <= 100; ++i) {
    //(1) 选择任务
    // int cmdcode = rand() % tasks.size();
    std::cout << "Please Enter your choic :";
    int choice = 0;
    std::cin >> choice;//输入choice
    if(choice==0){
      std::cout << "正在退出" << std::endl;
      return;
    }
    choice -= 1;
    //(2) 选择进程 -- 需要负载均衡 (随机数或是轮询 此处使用随机)
    int fd = rand() % PROCESSNUM;
    //(3) 发送任务 (任务码)
    std::cout << "Parent Process say : " << std::endl
                << "cmdcode = " << choice << " alread sendto "
                << channels[fd]._processname << channels[fd]._slaveryid
                << std::endl;
    write(channels[fd]._cmdfd, &choice, sizeof(choice));
    sleep(1);
    }
}

void QuitProcess(const std::vector<channel> channels)
{

    for(const auto &c:channels ){
        close(c._cmdfd);
        waitpid(c._slaveryid, nullptr, 0);
    }

    // version 1
    // int last = channels.size()-1;
    // for (; last >= 0;--last){
    //     close(channels[last]._cmdfd);
    //     sleep(3);
    //     waitpid(channels[last]._slaveryid, nullptr, 0);
    //     sleep(3);
    // }
    // /* 
    //     采用倒序关闭文件描述符的方法确实可以确保在结束时逐一关闭每个子进程对应的写端
    //     这会导致子进程读端在读到文件结尾（EOF）时退出循环
    //     并且子进程会调用 exit(0)
    //     进入僵尸状态等待父进程回收
    // */

    // for(const auto &c:channels ){
    //     close(c._cmdfd);
    //     }ss
    // sleep(5);
    // for (const auto& c : channels) {
    //     waitpid(c._slaveryid, nullptr, 0);
    // }
    // sleep(5);
    
}

int main() {
    //初始化任务列表
    LoadTask(&tasks);
    srand(time(nullptr) ^ getpid());  // 种随机数种子
    // 组织
    std::vector<channel> channels;
    InitProcessPool(&channels);

    // debug
    Debug(channels);
    // 2.控制子进程
    ControlSlaver(channels);
    // 3.清理收尾
    // sleep(1000);
    QuitProcess(channels);
    return 0;
}