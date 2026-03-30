#include "Task.hpp"
#include <unistd.h>
#include <cassert>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <sys/wait.h>
#include <sys/stat.h>

const int ProcessNum = 5;

std::vector<task_> tasks;

class channel//记录父进程视角下管理的子进程信息
{
public:
    channel(int cmdfd,pid_t id,const std::string& processname)
        :_cmdfd(cmdfd) //父进程与该子进程通道的写端
        ,_id(id)       //子进程的id
        ,_processname(processname) //子进程名
    {

    }

    int Return_cmdfd()
    {
        return _cmdfd;
    }

    pid_t Return__id()
    {
        return _id;
    }

    std::string Return_processname()
    {
        return _processname;
    }
private:
    int _cmdfd;
    pid_t _id;
    std::string _processname;
};

void Debug(std::vector<channel> channels)//读取子进程数据
{
    for(auto& x : channels)
    {
        std::cout << "_cmdfd : " << x.Return_cmdfd() << std::endl;
        std::cout << "_id : " << x.Return__id() << std::endl;
        std::cout << "_processname : " << x.Return_processname() << std::endl;
        std::cout << "------------------ " << std::endl;
    }
}

void slaver()//子进程读取父进程发送来的命令,并阻塞read执行，写端关闭后释放等待父进程读取
{
    while(1)
    {
        //子进程读取命令
        //对应执行相应命令
        //如果出现写端发送0，则关闭该子进程
        int cmdcode = 0;
        int n = read(0,&cmdcode,sizeof(int));//进行过重定向，只需要读0就可以读
        if(n == sizeof(int))//执行到对应的命令
        {
            std::cout <<"slaver get a cmdcode @["<< getpid() << "] : cmdcode : " << cmdcode << std :: endl;
            if(cmdcode < 0 || cmdcode > (int)tasks.size())
            {
                continue;
            }
            tasks[cmdcode - 1]();
            // tasks[cmdcode - 1]();//执行含有函数指针的vector内部的函数任务
            std::cout << "--------------------------" << std::endl;
        }   
        if(n == 0)//父进程写端关闭,文件描述符关闭则读到0
        {
            break;
        }
    }
}

void InitProcessPool(std::vector<channel> *channels)
{
    std::vector<int> parent_fds;//父进程每次的写端
    for(int i = 0;i < ProcessNum;i++)//循环进程数
    {
        int pipefd[2];//作为pipe函数传入
        int n = pipe(pipefd);//创建管道
        assert(!n);
        (void)n;

        pid_t id = fork();
        if(id < 0)
        {
            std::cerr << "fork failed" << std::endl;
        }
        else if(id == 0)//子进程
        {
            close(pipefd[1]);//关闭写端
            //子进程继承了父进程的数据，因此可以通过记录父进程每次创建管道留下的写端，依次删除
            for(auto& fd : parent_fds)
            {
                close(fd);
            }
            dup2(pipefd[0],0);
            slaver();//领取父进程的任务,并阻塞进行read，当所有子进程写端关闭子进程关闭
            std::cout << "fork process id :" << getpid() << "exit" << std::endl;
            exit(0);
        }
        else//父进程
        {
            close(pipefd[0]); //关闭读端
            std::string name = "Process" + std::to_string(i);
            parent_fds.push_back(pipefd[1]);//记录这次管道下的写端
            channels->push_back({pipefd[1],id,name});//记录每轮子进程数据到channels
            std::cout << "parent process id :" << getpid() << "Task allocation successful." << std::endl;
            
        }
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

void ControlSlaver(std::vector<channel> channels) 
{
    //读取任务选择菜单
    Menu();
    //用户选择任务进行发放
    //选择相应的子进程接收任务
    for(int i = 0;i < 100;i++)
    {
        std::cout << "Please Enter your choic :";
        int choice = 0;
        std::cin >> choice;
        srand(time(0));
        int fd = rand() % ProcessNum; //随机选择子进程进行任务
        if(choice==0)
        {
            std::cout << "正在退出" << std::endl;
            return;
        }
        std::cout << "Parent Process say : " << std::endl
                << "cmdcode = " << choice << " alread sendto "
                << channels[fd].Return_processname() << channels[fd].Return__id()
                << std::endl;        
        int n = write(channels[fd].Return_cmdfd(),&choice,sizeof(choice));//选择对应管道写入
        if(n < 0)
        {
            std::cerr << "write failed" << std::endl;
            exit(1); 
        }
        sleep(1);
    }
    
}

void QuitProcess(std::vector<channel> channels)
{
    //父进程进行子进程资源读取并释放
    for(auto& channel : channels)
    {
        int status = 0;
        close(channel.Return_cmdfd()); //关闭管道写端，触发子进程关闭
        pid_t ret = waitpid(channel.Return__id(),&status,0);
        if(ret > 0)
        {
            std::cout << "reaped pid=" << ret
                      << ", exit=" << (WIFEXITED(status) ? WEXITSTATUS(status) : -1)
                      << ", sig=" << (WIFSIGNALED(status) ? WTERMSIG(status) : 0)
                      << std::endl;
        }
    }
}

int main()
{
    //初始化任务列表
    Load_Task(&tasks);
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