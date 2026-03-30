#include<stdio.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>

#define SIZE 512
#define ZERO '\0'
#define SEP " "
#define NUM 32
#define SkipPath(p) do{ p+= strlen(p)-1;   while(*p!='/')p--;\
}while(0)

char cwd[SIZE*2];
char *gArgv[NUM];

int lastcode=0;

const char * GetUserName()
{
     const char *name = getenv("USER");
     if(name == NULL) return "None";
     return name;
}
const char * GetHostName()
{
    const char *hostname = getenv("HOSTNAME");
    if(hostname==NULL) return "None";
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
    if(home== NULL) return "/root";
    return home;
}
void MakeCommandLine()
{
    char line[SIZE];
    const char* username= GetUserName();
    const char* hostname= GetHostName();
    const char* cwd= GetCwd();

    SkipPath(cwd);
    snprintf(line,sizeof(line),"[%s@%s %s]> ",username,hostname,strlen(cwd)==1?"/":cwd+1);
    printf("%s",line);
    fflush(stdout);
}
int GetUserCommand(char command[],size_t n)
{
  char *s =fgets(command,n,stdin);
  if(s==NULL) return 1; 
  command[strlen(command)-1]=ZERO;
  return strlen(command);
}

void SplitCommand(char command[],size_t size)
{
   gArgv[0]=strtok(command,SEP);
   int index=1;
   while((gArgv[index++]=strtok(NULL,SEP)));
   (void)size;
}
void ExecuteCommand()
{

  pid_t id=fork();
  if(id < 0)exit(1);
  else if(id == 0)//子进程
  {
      execvp(gArgv[0],gArgv);
      exit(errno);
  }
  else //父进程
  {
      int status=0;
      pid_t rid = waitpid(id,&status,0);
      if(rid>0)
      {
        lastcode=WEXITSTATUS(status);
        if(lastcode!=0) printf("%s:%s:%d\n",gArgv[0],strerror(lastcode),lastcode);
      }
  }
}
void Cd()
{
    const char *path=gArgv[1];
    if(path==NULL)path =GetHome();
    chdir(path);
    //刷新环境变量
    char temp[SIZE*2];
    getcwd(temp,sizeof(temp));
    snprintf(cwd,sizeof(cwd),"PWD=%s",temp);
    putenv(cwd);
}
int CheckBuildin()
{
    int yes=0;
    const char * enter_cmd= gArgv[0];
    if(strcmp("cd",enter_cmd)==0)
    {
        yes=1;
        Cd();
    }
    else if(strcmp(enter_cmd,"echo")==0&&strcmp(gArgv[1],"$?")==0)
    {
        yes=1;
        printf("%d\n",lastcode);
        lastcode=0;
    }

    return yes;
}
int main()
{
    int quit=0;
  while(!quit)
  {
    //1.输出命令行
    MakeCommandLine();

    //2.获取输入命令

    char usercommand[SIZE];
    int n=  GetUserCommand(usercommand,sizeof(usercommand));
    if(n<=0) return 1;
    //3.命令行字符串分割
    SplitCommand(usercommand,sizeof(usercommand));  
    //4.检查是否为内建命令；
    n=CheckBuildin();
    if(n)continue;
    //执行命令
    ExecuteCommand();
  }
  return 0;

}
