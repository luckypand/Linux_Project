#include<iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
using namespace std;

const size_t SIZE = 512;
const char ZERO = '\0';
const char* SEP= " ";
const size_t NUM = 32;
int lastcode = 0; //进程状态信息码
char* Argv[NUM];
char cwd[SIZE * 2];
int exit_flag = 0;

const char* GetUsername()
{
    const char* name = getenv("USER");
    if(NULL == name) return "None";
    return name;
}

const char* GetHHostname()
{
    static char hostname[256];
    int ret = gethostname(hostname, sizeof(hostname));
    if(-1 == ret) return "None";
    hostname[sizeof(hostname) - 1] = '\0';
    return hostname;
}

const char * GetCwd()
{
    const char *cwd = getenv("PWD");
    if(cwd == NULL ) return "None";
    return cwd;
}

const char * GetHome()
{
    const char *home=getenv("HOME");
    if(home == NULL) return "/root";
    return home;
}

void MakeCommandLine()
{
    char line[SIZE];
    for(int i = 0;i < static_cast<int>(SIZE);i++)
    {
        line[i] = '\0';
    }
    const char* username = GetUsername();
    const char* hostname = GetHHostname();
    const char* pwd = GetCwd();

    snprintf(line,sizeof(line),"[%s@%s:%s]>",username,hostname,pwd);
    cout << line;
    fflush(stdout); //刷新缓冲
}

int GetUserCommand(char* command,size_t n)
{
    char *s =fgets(command,n,stdin);
    if(s == NULL) return 1; 
    if(*s == 'q') exit_flag = 1; //按下q允许退出
    command[strlen(command)-1]=ZERO;
    //   cout << s << endl;
    return strlen(command);
}

int SplitCommand_Letter(char* command) 
{
    Argv[0] = strtok(command,SEP); //依据空格开始分割命令
    int index = 1;
    while((Argv[index++] = strtok(NULL,SEP)) != nullptr);//依次分割字符串
    return index;
}
//需要解决，当只传入一个空格时，return的末尾下标为2

void Print_Array(char** Command_Letter,int n) //打印字符串
{
    for(int i = 0;i < n;i++)
    {
        cout << Command_Letter[i] << endl;
    }
}

void cd()
{
    const char* path = Argv[1];
    if(nullptr == path) path = GetHome();
    int n = chdir(path); //改变路径到命令行path
    if(-1 == n)
    {
        cout << "chdir failed" << endl;
        return;
    }
    //同步修改pwd的命令显示路径,即将PWD=xxx路径设置为环境变量
    char temp[SIZE * 2];
    char *s = getcwd(temp,sizeof(temp));//获取当前路径
    if(nullptr == s) 
    {
        cout << "getcwd failed" << endl;
        return;
    }
    if (setenv("PWD", temp, 1) == -1) {
        perror("setenv");
    }
}

int CheckBuildin()
{
    int buildin_flag = 0;

    if(0 == strcmp(Argv[0],"cd"))//命令为cd
    {
        //执行cd命令
        cd();
        buildin_flag = 1;
    }
    else if(0 == strcmp(Argv[0],"echo") && 0 == strcmp(Argv[1],"$?"))  //echo$命令
    {
        printf("%d\n",lastcode);//执行echo$? //没理解
        buildin_flag = 1;
    }

    return buildin_flag;
}

void Execution_external_command()
{
    //申请子进程
    //子进程处理外部命令，父进程负责接收子进程资源
    pid_t id = fork();
    if(id < 0) exit(1);
    else if(id == 0) //子进程处理外部命令
    {
        execvp(Argv[0],Argv); //该步骤在环境变量下寻找可执行文件
        exit(errno);
    }
    else //父进程
    {
        int status = 0;
        pid_t rid = waitpid(id,&status,0);
        if(rid > 0)
        {
            lastcode = WEXITSTATUS(status);
            if(lastcode)//正常退出
            {
                printf("[%s:%s:%d]",Argv[0],strerror(lastcode),lastcode);       
                cout << endl;         
            }
        }
    }
}

int main()
{   
    int quit = 0;

    while(!quit) 
    {
        //1.输出命令行
        MakeCommandLine();   
        char usercommand[SIZE];
        //2.获取输入命令
        int n = GetUserCommand(usercommand,sizeof(usercommand)); 
        if(n <= 0) return 1;
        //按下q退出
        if(exit_flag) 
        {
            exit_flag = 0;
            return 0;             
        }
        int Argv_Use = 0;
        //3.命令行字符串分割
        Argv_Use = SplitCommand_Letter(usercommand);        
        (void) Argv_Use;
        //4.检查是否为内建命令；内建命令父进程自己执行，外部命令申请子进程执行
        n = CheckBuildin();      
        //回传1代表是内建命令，检查并执行  
        if(n) continue; 
        //5.执行外部命令
        Execution_external_command();
    }

    return 0;
}